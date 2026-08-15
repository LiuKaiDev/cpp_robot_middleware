#include "detail/registry_client.hpp"

#include "detail/socket_io.hpp"
#include "detail/unix_socket.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <utility>

namespace mw::detail {
namespace {

int remainingPollTimeout(std::chrono::steady_clock::time_point deadline,
                         std::chrono::milliseconds timeout) noexcept {
    if (timeout.count() < 0) {
        return -1;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }
    const auto remaining = deadline - now;
    const auto rounded_up = std::chrono::duration_cast<std::chrono::milliseconds>(
        remaining + std::chrono::milliseconds{1} - std::chrono::nanoseconds{1});
    return static_cast<int>(
        std::min<std::int64_t>(rounded_up.count(), std::numeric_limits<int>::max()));
}

void readWithDeadline(int fd, void* buffer, std::size_t size, std::chrono::milliseconds timeout) {
    auto* destination = static_cast<std::uint8_t*>(buffer);
    std::size_t transferred = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          (timeout.count() < 0 ? std::chrono::hours{24 * 365 * 100} : timeout);

    while (transferred < size) {
        pollfd descriptor{fd, POLLIN, 0};
        const int poll_result = ::poll(&descriptor, 1, remainingPollTimeout(deadline, timeout));
        if (poll_result == 0) {
            throw MiddlewareError(ErrorCode::Timeout, "registry response timed out");
        }
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw MiddlewareError(ErrorCode::IoError, "registry poll failed");
        }
        if ((descriptor.revents & POLLNVAL) != 0) {
            throw MiddlewareError(ErrorCode::ConnectionLost, "registry connection is invalid");
        }

        const ssize_t received =
            ::recv(fd, destination + transferred, size - transferred, MSG_DONTWAIT);
        if (received > 0) {
            transferred += static_cast<std::size_t>(received);
            continue;
        }
        if (received == 0) {
            throw MiddlewareError(ErrorCode::ConnectionLost, "registry connection closed");
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        if (errno == ECONNRESET || errno == ENOTCONN) {
            throw MiddlewareError(ErrorCode::ConnectionLost, "registry connection lost");
        }
        throw MiddlewareError(ErrorCode::IoError, "registry receive failed");
    }
}

std::size_t checkedSize(std::uint64_t value) {
    if (value > std::numeric_limits<std::size_t>::max()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry size does not fit this process");
    }
    return static_cast<std::size_t>(value);
}

std::uint64_t readSingleId(const std::vector<std::uint8_t>& body) {
    PayloadReader reader{body};
    std::uint64_t id = 0;
    if (!reader.readU64(id) || !reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry response has an invalid ID payload");
    }
    return id;
}

RegistryEndpoint readEndpoint(const std::vector<std::uint8_t>& body) {
    PayloadReader reader{body};
    RegistryEndpoint endpoint;
    if (!reader.readU64(endpoint.topic_id) || !reader.readU64(endpoint.endpoint_id) ||
        !reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry response has an invalid endpoint payload");
    }
    return endpoint;
}

} // namespace

RegistryClient::RegistryClient(const RegistryConfig& config)
    : request_timeout_(config.request_timeout) {
    if (config.socket_path.empty()) {
        throw MiddlewareError(ErrorCode::InvalidArgument, "registry socket path must not be empty");
    }
    if (request_timeout_.count() <= 0) {
        throw MiddlewareError(ErrorCode::InvalidArgument,
                              "registry request timeout must be positive");
    }
    try {
        socket_ = connectUnixSocket(config.socket_path);
    } catch (const std::system_error& error) {
        throw MiddlewareError(ErrorCode::RegistryUnavailable, error.what());
    }
}

std::uint64_t RegistryClient::registerNode(const std::string& node_name) {
    PayloadWriter writer;
    writer.writeString(node_name);
    return readSingleId(request(Opcode::RegisterNode, writer.data(), request_timeout_));
}

void RegistryClient::unregisterNode(std::uint64_t node_id) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    (void)request(Opcode::UnregisterNode, writer.data(), request_timeout_);
}

RegistryEndpoint RegistryClient::advertise(std::uint64_t node_id, const std::string& topic_name,
                                           const std::string& type_name,
                                           const std::string& type_hash,
                                           std::size_t max_message_size) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    writer.writeString(topic_name);
    writer.writeString(type_name);
    writer.writeString(type_hash);
    writer.writeU64(max_message_size);
    return readEndpoint(request(Opcode::AdvertiseTopic, writer.data(), request_timeout_));
}

void RegistryClient::unadvertise(std::uint64_t node_id, std::uint64_t endpoint_id) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    writer.writeU64(endpoint_id);
    (void)request(Opcode::UnadvertiseTopic, writer.data(), request_timeout_);
}

RegistryEndpoint RegistryClient::subscribe(std::uint64_t node_id, const std::string& topic_name,
                                           const std::string& type_name,
                                           const std::string& type_hash,
                                           std::size_t max_message_size,
                                           const std::string& data_socket_path) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    writer.writeString(topic_name);
    writer.writeString(type_name);
    writer.writeString(type_hash);
    writer.writeU64(max_message_size);
    writer.writeString(data_socket_path);
    return readEndpoint(request(Opcode::SubscribeTopic, writer.data(), request_timeout_));
}

void RegistryClient::unsubscribe(std::uint64_t node_id, std::uint64_t endpoint_id) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    writer.writeU64(endpoint_id);
    (void)request(Opcode::UnsubscribeTopic, writer.data(), request_timeout_);
}

RegistryDiscovery RegistryClient::resolve(std::uint64_t node_id,
                                          std::uint64_t publisher_endpoint_id) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    writer.writeU64(publisher_endpoint_id);
    const auto body =
        request(Opcode::ResolveEndpoint, writer.data(), std::chrono::milliseconds{-1});
    PayloadReader reader{body};
    RegistryDiscovery discovery;
    std::uint64_t max_message_size = 0;
    if (!reader.readU64(discovery.subscriber_endpoint_id) ||
        !reader.readString(discovery.data_socket_path) || !reader.readU64(max_message_size) ||
        !reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry response has invalid discovery data");
    }
    discovery.max_message_size = checkedSize(max_message_size);
    return discovery;
}

std::vector<RegistryNodeInfo> RegistryClient::listNodes() {
    const auto body = request(Opcode::ListNodes, {}, request_timeout_);
    PayloadReader reader{body};
    std::uint32_t count = 0;
    if (!reader.readU32(count)) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid node list response");
    }
    std::vector<RegistryNodeInfo> nodes;
    nodes.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        RegistryNodeInfo node;
        if (!reader.readU64(node.node_id) || !reader.readString(node.node_name)) {
            throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid node list entry");
        }
        nodes.push_back(std::move(node));
    }
    if (!reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage, "trailing node list data");
    }
    return nodes;
}

std::vector<RegistryTopicInfo> RegistryClient::listTopics() {
    const auto body = request(Opcode::ListTopics, {}, request_timeout_);
    PayloadReader reader{body};
    std::uint32_t count = 0;
    if (!reader.readU32(count)) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid topic list response");
    }
    std::vector<RegistryTopicInfo> topics;
    topics.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        RegistryTopicInfo topic;
        if (!reader.readU64(topic.topic_id) || !reader.readString(topic.topic_name)) {
            throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid topic list entry");
        }
        topics.push_back(std::move(topic));
    }
    if (!reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage, "trailing topic list data");
    }
    return topics;
}

RegistryTopicInfo RegistryClient::queryTopic(const std::string& topic_name) {
    PayloadWriter writer;
    writer.writeString(topic_name);
    const auto body = request(Opcode::QueryTopic, writer.data(), request_timeout_);
    PayloadReader reader{body};
    RegistryTopicInfo topic;
    std::uint64_t max_message_size = 0;
    std::uint64_t publisher_count = 0;
    std::uint64_t subscriber_count = 0;
    if (!reader.readU64(topic.topic_id) || !reader.readString(topic.topic_name) ||
        !reader.readString(topic.type_name) || !reader.readString(topic.type_hash) ||
        !reader.readU64(max_message_size) || !reader.readU64(publisher_count) ||
        !reader.readU64(subscriber_count) || !reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid topic info response");
    }
    topic.max_message_size = checkedSize(max_message_size);
    topic.publisher_count = checkedSize(publisher_count);
    topic.subscriber_count = checkedSize(subscriber_count);
    return topic;
}

std::vector<std::uint8_t> RegistryClient::request(Opcode opcode,
                                                  const std::vector<std::uint8_t>& payload,
                                                  std::chrono::milliseconds timeout) {
    if (!socket_) {
        throw MiddlewareError(ErrorCode::ConnectionLost, "registry connection is closed");
    }
    if (next_request_id_ == 0U) {
        throw MiddlewareError(ErrorCode::InvalidState, "registry request ID space exhausted");
    }
    const std::uint32_t request_id = next_request_id_++;
    const auto frame = encodeControlFrame(opcode, request_id, payload);
    const IoResult write_result = writeAll(socket_.get(), frame.data(), frame.size());
    if (write_result.status != IoStatus::Complete) {
        socket_.reset();
        throw MiddlewareError(write_result.status == IoStatus::Closed ? ErrorCode::ConnectionLost
                                                                      : ErrorCode::IoError,
                              "failed to send registry request");
    }

    const ControlFrame response = receiveFrame(timeout);
    if (response.header.opcode != Opcode::Response) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry sent a non-response opcode");
    }
    if (response.header.request_id != request_id) {
        throw MiddlewareError(ErrorCode::InvalidRequestId, "registry response ID mismatch");
    }
    const auto envelope = decodeResponsePayload(response.payload);
    if (!envelope.has_value()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid registry response body");
    }
    if (envelope->error != ErrorCode::Ok) {
        throw MiddlewareError(envelope->error, envelope->message.empty()
                                                   ? errorMessage(envelope->error)
                                                   : envelope->message);
    }
    return envelope->body;
}

ControlFrame RegistryClient::receiveFrame(std::chrono::milliseconds timeout) {
    std::array<std::uint8_t, kControlHeaderSize> encoded_header{};
    readWithDeadline(socket_.get(), encoded_header.data(), encoded_header.size(), timeout);
    const auto header = decodeControlHeader(encoded_header.data(), encoded_header.size());
    if (!header.has_value()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid registry header size");
    }
    const ControlHeaderValidation validation = validateControlHeader(*header);
    if (validation == ControlHeaderValidation::BadMagic) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid registry magic");
    }
    if (validation == ControlHeaderValidation::UnsupportedVersion) {
        throw MiddlewareError(ErrorCode::UnsupportedProtocolVersion,
                              "unsupported registry protocol version");
    }
    if (validation == ControlHeaderValidation::PayloadTooLarge) {
        throw MiddlewareError(ErrorCode::MessageTooLarge, "registry response is too large");
    }

    ControlFrame frame{*header, std::vector<std::uint8_t>(header->payload_size)};
    readWithDeadline(socket_.get(), frame.payload.data(), frame.payload.size(), timeout);
    return frame;
}

RegistrySession::RegistrySession(std::string node_name, const RegistryConfig& config)
    : client_(config), node_id_(client_.registerNode(node_name)) {}

RegistrySession::~RegistrySession() {
    if (node_id_ != 0U) {
        try {
            client_.unregisterNode(node_id_);
        } catch (...) {
        }
    }
}

RegistryEndpoint RegistrySession::advertise(const std::string& topic_name,
                                            const PublisherConfig& config) {
    return client_.advertise(node_id_, topic_name, config.type_name, config.type_hash,
                             config.max_message_size);
}

void RegistrySession::unadvertise(std::uint64_t endpoint_id) noexcept {
    try {
        client_.unadvertise(node_id_, endpoint_id);
    } catch (...) {
    }
}

RegistryEndpoint RegistrySession::subscribe(const std::string& topic_name,
                                            const SubscriberConfig& config) {
    return client_.subscribe(node_id_, topic_name, config.type_name, config.type_hash,
                             config.max_message_size, config.socket_path);
}

void RegistrySession::unsubscribe(std::uint64_t endpoint_id) noexcept {
    try {
        client_.unsubscribe(node_id_, endpoint_id);
    } catch (...) {
    }
}

RegistryDiscovery RegistrySession::resolve(std::uint64_t publisher_endpoint_id) {
    return client_.resolve(node_id_, publisher_endpoint_id);
}

} // namespace mw::detail
