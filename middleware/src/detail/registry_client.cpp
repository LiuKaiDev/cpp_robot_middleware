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

RegistryRegistration readRegistration(const std::vector<std::uint8_t>& body) {
    PayloadReader reader{body};
    RegistryRegistration registration;
    if (!reader.readU64(registration.node_id) || !reader.readU64(registration.session_id) ||
        registration.node_id == 0U || registration.session_id == 0U || !reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry response has an invalid registration payload");
    }
    return registration;
}

RegistryEndpoint readEndpoint(const std::vector<std::uint8_t>& body) {
    PayloadReader reader{body};
    RegistryEndpoint endpoint;
    if (!reader.readU64(endpoint.topic_id) || !reader.readU64(endpoint.endpoint_id) ||
        !reader.readString(endpoint.pool.shm_name) || !reader.readU64(endpoint.pool.pool_id) ||
        !reader.readU64(endpoint.pool.segment_size) ||
        !reader.readU16(endpoint.pool.layout_version) || !reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry response has an invalid endpoint payload");
    }
    endpoint.pool.topic_id = endpoint.topic_id;
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
    if (!validLivenessConfig(config.liveness)) {
        throw MiddlewareError(ErrorCode::InvalidArgument, "registry liveness timing is invalid");
    }
    try {
        socket_ = connectUnixSocket(config.socket_path);
    } catch (const std::system_error& error) {
        throw MiddlewareError(ErrorCode::RegistryUnavailable, error.what());
    }
}

RegistryRegistration RegistryClient::registerNode(const std::string& node_name) {
    PayloadWriter writer;
    writer.writeString(node_name);
    return readRegistration(request(Opcode::RegisterNode, writer.data(), request_timeout_));
}

void RegistryClient::unregisterNode(std::uint64_t node_id) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    (void)request(Opcode::UnregisterNode, writer.data(), request_timeout_);
}

void RegistryClient::attachHeartbeat(std::uint64_t node_id, std::uint64_t session_id) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    writer.writeU64(session_id);
    (void)request(Opcode::AttachHeartbeat, writer.data(), request_timeout_);
}

HeartbeatResponse RegistryClient::heartbeat(std::uint64_t node_id, std::uint64_t session_id) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    writer.writeU64(session_id);
    const auto body = request(Opcode::Heartbeat, writer.data(), request_timeout_);
    PayloadReader reader{body};
    HeartbeatResponse response;
    std::uint16_t state = 0;
    std::uint32_t count = 0;
    if (!reader.readU16(state) || !reader.readU32(count)) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry heartbeat response is malformed");
    }
    response.state = static_cast<LivenessState>(state);
    if (response.state != LivenessState::Alive && response.state != LivenessState::Suspected) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry heartbeat response has invalid state");
    }
    response.events.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        RegistryPeerEvent event;
        std::uint16_t kind = 0;
        if (!reader.readU16(kind) || !reader.readU64(event.target_endpoint_id) ||
            !reader.readU64(event.dead_endpoint_id) || !reader.readU64(event.topic_id) ||
            !reader.readU64(event.resource_id)) {
            throw MiddlewareError(ErrorCode::InvalidControlMessage,
                                  "registry heartbeat event is malformed");
        }
        event.kind = static_cast<PeerEventKind>(kind);
        if (event.kind != PeerEventKind::PublisherDead &&
            event.kind != PeerEventKind::SubscriberDead) {
            throw MiddlewareError(ErrorCode::InvalidControlMessage,
                                  "registry heartbeat event kind is invalid");
        }
        response.events.push_back(event);
    }
    if (!reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry heartbeat response has trailing data");
    }
    return response;
}

RegistryEndpoint RegistryClient::advertise(std::uint64_t node_id, const std::string& topic_name,
                                           const std::string& type_name,
                                           const std::string& type_hash,
                                           std::size_t max_message_size, TransportType transport,
                                           const PoolDescriptor& pool) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    writer.writeString(topic_name);
    writer.writeString(type_name);
    writer.writeString(type_hash);
    writer.writeU64(max_message_size);
    writer.writeU16(static_cast<std::uint16_t>(transport));
    writer.writeString(pool.shm_name);
    writer.writeU64(pool.pool_id);
    writer.writeU64(pool.segment_size);
    writer.writeU16(pool.layout_version);
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
                                           const std::string& data_socket_path,
                                           TransportType transport, const QueueDescriptor& queue) {
    PayloadWriter writer;
    writer.writeU64(node_id);
    writer.writeString(topic_name);
    writer.writeString(type_name);
    writer.writeString(type_hash);
    writer.writeU64(max_message_size);
    writer.writeString(data_socket_path);
    writer.writeU16(static_cast<std::uint16_t>(transport));
    writer.writeString(queue.shm_name);
    writer.writeU64(queue.queue_id);
    writer.writeU64(queue.segment_size);
    writer.writeU32(queue.capacity);
    writer.writeU16(queue.layout_version);
    writer.writeU16(static_cast<std::uint16_t>(queue.overflow_policy));
    writer.writeU64(queue.block_timeout_ms);
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
    std::uint16_t transport = 0;
    std::uint32_t count = 0;
    if (!reader.readU64(discovery.topic_id) || !reader.readU16(transport) ||
        !reader.readString(discovery.pool.shm_name) || !reader.readU64(discovery.pool.pool_id) ||
        !reader.readU64(discovery.pool.segment_size) ||
        !reader.readU16(discovery.pool.layout_version) || !reader.readU32(count)) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry response has invalid discovery data");
    }
    discovery.transport = static_cast<TransportType>(transport);
    discovery.pool.topic_id = discovery.topic_id;
    discovery.subscribers.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        RegistrySubscriber subscriber;
        std::uint64_t max_message_size = 0;
        if (!reader.readU64(subscriber.endpoint_id) ||
            !reader.readString(subscriber.data_socket_path) || !reader.readU64(max_message_size) ||
            !reader.readString(subscriber.queue.shm_name) ||
            !reader.readU64(subscriber.queue.queue_id) ||
            !reader.readU64(subscriber.queue.segment_size) ||
            !reader.readU32(subscriber.queue.capacity) ||
            !reader.readU16(subscriber.queue.layout_version)) {
            throw MiddlewareError(ErrorCode::InvalidControlMessage,
                                  "registry response has invalid subscriber discovery data");
        }
        std::uint16_t overflow_policy = 0;
        if (!reader.readU16(overflow_policy) ||
            !reader.readU64(subscriber.queue.block_timeout_ms)) {
            throw MiddlewareError(ErrorCode::InvalidControlMessage,
                                  "registry response has invalid subscriber queue data");
        }
        subscriber.queue.overflow_policy = static_cast<OverflowPolicy>(overflow_policy);
        subscriber.max_message_size = checkedSize(max_message_size);
        discovery.subscribers.push_back(std::move(subscriber));
    }
    if (discovery.subscribers.empty() || !reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage,
                              "registry response has no subscriber discovery data");
    }
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
        std::uint16_t liveness = 0;
        if (!reader.readU64(node.node_id) || !reader.readString(node.node_name) ||
            !reader.readU16(liveness)) {
            throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid node list entry");
        }
        node.liveness = static_cast<LivenessState>(liveness);
        if (node.liveness != LivenessState::Alive && node.liveness != LivenessState::Suspected) {
            throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid node liveness state");
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
    std::uint16_t transport = 0;
    if (!reader.readU64(topic.topic_id) || !reader.readString(topic.topic_name) ||
        !reader.readString(topic.type_name) || !reader.readString(topic.type_hash) ||
        !reader.readU16(transport) || !reader.readU64(max_message_size) ||
        !reader.readU64(publisher_count) || !reader.readU64(subscriber_count) ||
        !reader.readString(topic.pool.shm_name) || !reader.readU64(topic.pool.pool_id) ||
        !reader.readU64(topic.pool.segment_size) || !reader.readU16(topic.pool.layout_version) ||
        !reader.empty()) {
        throw MiddlewareError(ErrorCode::InvalidControlMessage, "invalid topic info response");
    }
    topic.max_message_size = checkedSize(max_message_size);
    topic.transport = static_cast<TransportType>(transport);
    topic.publisher_count = checkedSize(publisher_count);
    topic.subscriber_count = checkedSize(subscriber_count);
    topic.pool.topic_id = topic.topic_id;
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
    : client_(config), config_(config) {
    const RegistryRegistration registration = client_.registerNode(node_name);
    node_id_ = registration.node_id;
    session_id_ = registration.session_id;
    try {
        heartbeat_client_ = std::make_unique<RegistryClient>(config_);
        heartbeat_client_->attachHeartbeat(node_id_, session_id_);
        heartbeat_thread_ = std::thread([this] { heartbeatLoop(); });
    } catch (...) {
        try {
            client_.unregisterNode(node_id_);
        } catch (...) {
        }
        node_id_ = 0U;
        session_id_ = 0U;
        throw;
    }
}

RegistrySession::~RegistrySession() {
    {
        std::lock_guard<std::mutex> lock{heartbeat_mutex_};
        stop_heartbeat_ = true;
    }
    heartbeat_condition_.notify_all();
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    heartbeat_client_.reset();
    if (node_id_ != 0U) {
        try {
            client_.unregisterNode(node_id_);
        } catch (...) {
        }
    }
}

RegistryEndpoint RegistrySession::advertise(const std::string& topic_name,
                                            const PublisherConfig& config,
                                            const PoolDescriptor& pool) {
    return client_.advertise(node_id_, topic_name, config.type_name, config.type_hash,
                             config.max_message_size, config.transport, pool);
}

void RegistrySession::unadvertise(std::uint64_t endpoint_id) noexcept {
    try {
        client_.unadvertise(node_id_, endpoint_id);
    } catch (...) {
    }
}

RegistryEndpoint RegistrySession::subscribe(const std::string& topic_name,
                                            const SubscriberConfig& config,
                                            const QueueDescriptor& queue) {
    return client_.subscribe(node_id_, topic_name, config.type_name, config.type_hash,
                             config.max_message_size, config.socket_path, config.transport, queue);
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

std::vector<RegistryPeerEvent> RegistrySession::takePeerEvents(std::uint64_t target_endpoint_id) {
    std::lock_guard<std::mutex> lock{event_mutex_};
    std::vector<RegistryPeerEvent> selected;
    for (auto iterator = peer_events_.begin(); iterator != peer_events_.end();) {
        if (iterator->target_endpoint_id == target_endpoint_id) {
            selected.push_back(*iterator);
            iterator = peer_events_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    return selected;
}

void RegistrySession::heartbeatLoop() noexcept {
    constexpr std::size_t kMaxPendingPeerEvents = 4096U;
    std::unique_lock<std::mutex> lock{heartbeat_mutex_};
    while (!stop_heartbeat_) {
        if (heartbeat_condition_.wait_for(lock, config_.liveness.heartbeat_interval,
                                          [this] { return stop_heartbeat_; })) {
            break;
        }
        lock.unlock();
        try {
            HeartbeatResponse response = heartbeat_client_->heartbeat(node_id_, session_id_);
            if (!response.events.empty()) {
                std::lock_guard<std::mutex> event_lock{event_mutex_};
                for (const RegistryPeerEvent& event : response.events) {
                    const auto duplicate = std::find_if(
                        peer_events_.begin(), peer_events_.end(),
                        [&event](const RegistryPeerEvent& existing) {
                            return existing.kind == event.kind &&
                                   existing.target_endpoint_id == event.target_endpoint_id &&
                                   existing.dead_endpoint_id == event.dead_endpoint_id;
                        });
                    if (duplicate == peer_events_.end()) {
                        if (peer_events_.size() == kMaxPendingPeerEvents) {
                            peer_events_.erase(peer_events_.begin());
                        }
                        peer_events_.push_back(event);
                    }
                }
            }
        } catch (...) {
            return;
        }
        lock.lock();
    }
}

} // namespace mw::detail
