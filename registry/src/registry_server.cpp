#include <mw/registry/registry_server.hpp>

#include <mw/registry/registry_state.hpp>

#include "detail/control_protocol.hpp"
#include "detail/unique_fd.hpp"
#include "detail/unix_socket.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <system_error>
#include <utility>
#include <vector>

namespace mw::registry {
namespace {

struct ClientConnection {
    ConnectionId connection_id{0};
    detail::UniqueFd fd;
    std::vector<std::uint8_t> input;
    std::vector<std::uint8_t> output;
    std::size_t output_offset{0};
    bool close_after_write{false};
};

struct PendingDiscovery {
    int fd{-1};
    ConnectionId connection_id{0};
    std::uint64_t node_id{0};
    std::uint64_t publisher_endpoint_id{0};
    std::uint32_t request_id{0};
};

detail::PayloadWriter endpointBody(const EndpointResult& result) {
    detail::PayloadWriter writer;
    writer.writeU64(result.topic_id);
    writer.writeU64(result.endpoint_id);
    writer.writeString(result.pool.shm_name);
    writer.writeU64(result.pool.pool_id);
    writer.writeU64(result.pool.segment_size);
    writer.writeU16(result.pool.layout_version);
    return writer;
}

detail::PayloadWriter discoveryBody(const DiscoveryResult& result) {
    detail::PayloadWriter writer;
    writer.writeU64(result.topic_id);
    writer.writeU16(static_cast<std::uint16_t>(result.transport));
    writer.writeString(result.pool.shm_name);
    writer.writeU64(result.pool.pool_id);
    writer.writeU64(result.pool.segment_size);
    writer.writeU16(result.pool.layout_version);
    writer.writeU32(static_cast<std::uint32_t>(result.subscribers.size()));
    for (const DiscoveredSubscriber& subscriber : result.subscribers) {
        writer.writeU64(subscriber.endpoint_id);
        writer.writeString(subscriber.data_socket_path);
        writer.writeU64(subscriber.max_message_size);
        writer.writeString(subscriber.queue.shm_name);
        writer.writeU64(subscriber.queue.queue_id);
        writer.writeU64(subscriber.queue.segment_size);
        writer.writeU32(subscriber.queue.capacity);
        writer.writeU16(subscriber.queue.layout_version);
        writer.writeU16(subscriber.queue.overflow_policy);
        writer.writeU64(subscriber.queue.block_timeout_ms);
    }
    return writer;
}

} // namespace

struct RegistryServer::Impl {
    explicit Impl(std::string path)
        : socket_path(std::move(path)), listener(detail::UnixListener::create(socket_path, 64)) {
        const int descriptor = ::epoll_create1(EPOLL_CLOEXEC);
        if (descriptor < 0) {
            throw std::system_error(errno, std::generic_category(), "epoll_create1");
        }
        epoll_fd = detail::UniqueFd{descriptor};
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = listener.fd();
        if (::epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, listener.fd(), &event) < 0) {
            throw std::system_error(errno, std::generic_category(), "epoll_ctl(listener)");
        }
    }

    void pollOnce(int timeout_ms) {
        std::array<epoll_event, 32> events{};
        int count = 0;
        do {
            count = ::epoll_wait(epoll_fd.get(), events.data(), static_cast<int>(events.size()),
                                 timeout_ms);
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            throw std::system_error(errno, std::generic_category(), "epoll_wait");
        }

        for (int index = 0; index < count; ++index) {
            const int fd = events[static_cast<std::size_t>(index)].data.fd;
            const std::uint32_t flags = events[static_cast<std::size_t>(index)].events;
            if (fd == listener.fd()) {
                acceptConnections();
                continue;
            }
            if (clients.count(fd) == 0U) {
                continue;
            }
            if ((flags & EPOLLIN) != 0U) {
                readClient(fd);
            }
            if (clients.count(fd) != 0U && (flags & EPOLLOUT) != 0U) {
                writeClient(fd);
            }
            if (clients.count(fd) != 0U && (flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U &&
                (flags & EPOLLIN) == 0U) {
                removeClient(fd);
            }
        }
    }

    void acceptConnections() {
        while (true) {
            auto accepted = listener.accept();
            if (!accepted.has_value()) {
                return;
            }
            const int fd = accepted->get();
            ClientConnection connection;
            connection.connection_id = next_connection_id++;
            connection.fd = std::move(*accepted);
            clients.emplace(fd, std::move(connection));

            epoll_event event{};
            event.events = EPOLLIN | EPOLLRDHUP;
            event.data.fd = fd;
            if (::epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, fd, &event) < 0) {
                clients.erase(fd);
                throw std::system_error(errno, std::generic_category(), "epoll_ctl(client add)");
            }
        }
    }

    void readClient(int fd) {
        auto iterator = clients.find(fd);
        if (iterator == clients.end()) {
            return;
        }

        std::array<std::uint8_t, 4096> buffer{};
        while (true) {
            const ssize_t received = ::recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
            if (received > 0) {
                iterator->second.input.insert(iterator->second.input.end(), buffer.begin(),
                                              buffer.begin() + received);
                if (iterator->second.input.size() >
                    detail::kControlHeaderSize + detail::kMaxControlPayloadSize) {
                    queueResponse(fd, 0, ErrorCode::MessageTooLarge, {});
                    iterator->second.close_after_write = true;
                    iterator->second.input.clear();
                    return;
                }
                parseFrames(fd);
                if (clients.count(fd) == 0U) {
                    return;
                }
                iterator = clients.find(fd);
                if (iterator->second.close_after_write) {
                    return;
                }
                continue;
            }
            if (received == 0) {
                if (iterator->second.output_offset < iterator->second.output.size()) {
                    iterator->second.close_after_write = true;
                    updateInterest(fd);
                } else {
                    removeClient(fd);
                }
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            removeClient(fd);
            return;
        }
    }

    void parseFrames(int fd) {
        ClientConnection& client = clients.at(fd);
        while (client.input.size() >= detail::kControlHeaderSize) {
            const auto header =
                detail::decodeControlHeader(client.input.data(), detail::kControlHeaderSize);
            if (!header.has_value()) {
                queueResponse(fd, 0, ErrorCode::InvalidControlMessage, {});
                client.close_after_write = true;
                client.input.clear();
                return;
            }

            const detail::ControlHeaderValidation validation =
                detail::validateControlHeader(*header);
            if (validation != detail::ControlHeaderValidation::Valid) {
                ErrorCode error = ErrorCode::InvalidControlMessage;
                if (validation == detail::ControlHeaderValidation::UnsupportedVersion) {
                    error = ErrorCode::UnsupportedProtocolVersion;
                } else if (validation == detail::ControlHeaderValidation::PayloadTooLarge) {
                    error = ErrorCode::MessageTooLarge;
                }
                queueResponse(fd, header->request_id, error, {});
                client.close_after_write = true;
                client.input.clear();
                return;
            }

            const std::size_t frame_size = detail::kControlHeaderSize + header->payload_size;
            if (client.input.size() < frame_size) {
                return;
            }

            std::vector<std::uint8_t> payload(
                client.input.begin() + static_cast<std::ptrdiff_t>(detail::kControlHeaderSize),
                client.input.begin() + static_cast<std::ptrdiff_t>(frame_size));
            client.input.erase(client.input.begin(),
                               client.input.begin() + static_cast<std::ptrdiff_t>(frame_size));

            if (!detail::isKnownRequestOpcode(header->opcode)) {
                queueResponse(fd, header->request_id, ErrorCode::UnknownOpcode, {});
                continue;
            }
            dispatch(fd, *header, payload);
        }
    }

    void dispatch(int fd, const detail::ControlHeader& header,
                  const std::vector<std::uint8_t>& payload) {
        ClientConnection& client = clients.at(fd);
        detail::PayloadReader reader{payload};

        switch (header.opcode) {
        case detail::Opcode::RegisterNode: {
            std::string node_name;
            if (!reader.readString(node_name) || !reader.empty()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidControlMessage, {});
                return;
            }
            const IdResult result = state.registerNode(client.connection_id, node_name);
            detail::PayloadWriter body;
            body.writeU64(result.id);
            queueResponse(fd, header.request_id, result.error, body.data());
            return;
        }
        case detail::Opcode::UnregisterNode: {
            std::uint64_t node_id = 0;
            if (!reader.readU64(node_id) || !reader.empty()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidControlMessage, {});
                return;
            }
            const ErrorCode error = state.unregisterNode(client.connection_id, node_id);
            cleanupPending();
            queueResponse(fd, header.request_id, error, {});
            return;
        }
        case detail::Opcode::AdvertiseTopic: {
            std::uint64_t node_id = 0;
            std::uint64_t max_message_size = 0;
            std::uint16_t transport = 0;
            std::string topic_name;
            std::string type_name;
            std::string type_hash;
            SharedPoolMetadata pool;
            if (!reader.readU64(node_id) || !reader.readString(topic_name) ||
                !reader.readString(type_name) || !reader.readString(type_hash) ||
                !reader.readU64(max_message_size) || !reader.readU16(transport) ||
                !reader.readString(pool.shm_name) || !reader.readU64(pool.pool_id) ||
                !reader.readU64(pool.segment_size) || !reader.readU16(pool.layout_version) ||
                !reader.empty()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidControlMessage, {});
                return;
            }
            if (max_message_size > std::numeric_limits<std::uint32_t>::max()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidArgument, {});
                return;
            }
            const EndpointResult result =
                state.advertise(client.connection_id, node_id, topic_name, type_name, type_hash,
                                static_cast<std::size_t>(max_message_size),
                                static_cast<TransportType>(transport), std::move(pool));
            const auto body = endpointBody(result);
            queueResponse(fd, header.request_id, result.error, body.data());
            return;
        }
        case detail::Opcode::UnadvertiseTopic: {
            std::uint64_t node_id = 0;
            std::uint64_t endpoint_id = 0;
            if (!reader.readU64(node_id) || !reader.readU64(endpoint_id) || !reader.empty()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidControlMessage, {});
                return;
            }
            const ErrorCode error = state.unadvertise(client.connection_id, node_id, endpoint_id);
            pending.erase(endpoint_id);
            queueResponse(fd, header.request_id, error, {});
            return;
        }
        case detail::Opcode::SubscribeTopic: {
            std::uint64_t node_id = 0;
            std::uint64_t max_message_size = 0;
            std::uint16_t transport = 0;
            std::string topic_name;
            std::string type_name;
            std::string type_hash;
            std::string data_socket_path;
            SharedQueueMetadata queue;
            if (!reader.readU64(node_id) || !reader.readString(topic_name) ||
                !reader.readString(type_name) || !reader.readString(type_hash) ||
                !reader.readU64(max_message_size) || !reader.readString(data_socket_path) ||
                !reader.readU16(transport) || !reader.readString(queue.shm_name) ||
                !reader.readU64(queue.queue_id) || !reader.readU64(queue.segment_size) ||
                !reader.readU32(queue.capacity) || !reader.readU16(queue.layout_version) ||
                !reader.readU16(queue.overflow_policy) ||
                !reader.readU64(queue.block_timeout_ms) || !reader.empty()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidControlMessage, {});
                return;
            }
            if (max_message_size > std::numeric_limits<std::uint32_t>::max()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidArgument, {});
                return;
            }
            const EndpointResult result =
                state.subscribe(client.connection_id, node_id, topic_name, type_name, type_hash,
                                static_cast<std::size_t>(max_message_size), data_socket_path,
                                static_cast<TransportType>(transport), std::move(queue));
            const auto body = endpointBody(result);
            queueResponse(fd, header.request_id, result.error, body.data());
            if (result.error == ErrorCode::Ok) {
                completePending();
            }
            return;
        }
        case detail::Opcode::UnsubscribeTopic: {
            std::uint64_t node_id = 0;
            std::uint64_t endpoint_id = 0;
            if (!reader.readU64(node_id) || !reader.readU64(endpoint_id) || !reader.empty()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidControlMessage, {});
                return;
            }
            queueResponse(fd, header.request_id,
                          state.unsubscribe(client.connection_id, node_id, endpoint_id), {});
            return;
        }
        case detail::Opcode::ResolveEndpoint: {
            std::uint64_t node_id = 0;
            std::uint64_t endpoint_id = 0;
            if (!reader.readU64(node_id) || !reader.readU64(endpoint_id) || !reader.empty()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidControlMessage, {});
                return;
            }
            const DiscoveryResult result =
                state.resolve(client.connection_id, node_id, endpoint_id);
            if (result.error == ErrorCode::TopicNotFound) {
                pending[endpoint_id] = PendingDiscovery{fd, client.connection_id, node_id,
                                                        endpoint_id, header.request_id};
                return;
            }
            const auto body = discoveryBody(result);
            queueResponse(fd, header.request_id, result.error, body.data());
            return;
        }
        case detail::Opcode::ListNodes: {
            if (!reader.empty()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidControlMessage, {});
                return;
            }
            const auto nodes = state.listNodes();
            detail::PayloadWriter body;
            body.writeU32(static_cast<std::uint32_t>(nodes.size()));
            for (const NodeRecord& node : nodes) {
                body.writeU64(node.node_id);
                body.writeString(node.node_name);
            }
            queueResponse(fd, header.request_id, ErrorCode::Ok, body.data());
            return;
        }
        case detail::Opcode::ListTopics: {
            if (!reader.empty()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidControlMessage, {});
                return;
            }
            const auto topics = state.listTopics();
            detail::PayloadWriter body;
            body.writeU32(static_cast<std::uint32_t>(topics.size()));
            for (const TopicRecord& topic : topics) {
                body.writeU64(topic.topic_id);
                body.writeString(topic.topic_name);
            }
            queueResponse(fd, header.request_id, ErrorCode::Ok, body.data());
            return;
        }
        case detail::Opcode::QueryTopic: {
            std::string topic_name;
            if (!reader.readString(topic_name) || !reader.empty()) {
                queueResponse(fd, header.request_id, ErrorCode::InvalidControlMessage, {});
                return;
            }
            const auto topic = state.queryTopic(topic_name);
            if (!topic.has_value()) {
                queueResponse(fd, header.request_id, ErrorCode::TopicNotFound, {});
                return;
            }
            detail::PayloadWriter body;
            body.writeU64(topic->topic_id);
            body.writeString(topic->topic_name);
            body.writeString(topic->type_name);
            body.writeString(topic->type_hash);
            body.writeU16(static_cast<std::uint16_t>(topic->transport));
            body.writeU64(topic->max_message_size);
            body.writeU64(topic->publisher_endpoint.has_value() ? 1U : 0U);
            body.writeU64(topic->subscriber_endpoints.size());
            body.writeString(topic->pool.shm_name);
            body.writeU64(topic->pool.pool_id);
            body.writeU64(topic->pool.segment_size);
            body.writeU16(topic->pool.layout_version);
            queueResponse(fd, header.request_id, ErrorCode::Ok, body.data());
            return;
        }
        case detail::Opcode::Response:
            break;
        }
        queueResponse(fd, header.request_id, ErrorCode::UnknownOpcode, {});
    }

    void completePending() {
        for (auto iterator = pending.begin(); iterator != pending.end();) {
            const PendingDiscovery request = iterator->second;
            const DiscoveryResult result = state.resolve(request.connection_id, request.node_id,
                                                         request.publisher_endpoint_id);
            if (result.error == ErrorCode::TopicNotFound) {
                ++iterator;
                continue;
            }
            if (clients.count(request.fd) != 0U) {
                const auto body = discoveryBody(result);
                queueResponse(request.fd, request.request_id, result.error, body.data());
            }
            iterator = pending.erase(iterator);
        }
    }

    void cleanupPending() {
        for (auto iterator = pending.begin(); iterator != pending.end();) {
            if (!state.publisherEndpoint(iterator->first).has_value()) {
                iterator = pending.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    void queueResponse(int fd, std::uint32_t request_id, ErrorCode error,
                       const std::vector<std::uint8_t>& body) {
        auto iterator = clients.find(fd);
        if (iterator == clients.end()) {
            return;
        }
        std::vector<std::uint8_t> frame;
        try {
            const auto payload = detail::encodeResponsePayload(
                error, error == ErrorCode::Ok ? std::string{} : errorMessage(error), body);
            frame = detail::encodeControlFrame(detail::Opcode::Response, request_id, payload);
        } catch (const std::length_error&) {
            const auto payload = detail::encodeResponsePayload(
                ErrorCode::MessageTooLarge, errorMessage(ErrorCode::MessageTooLarge));
            frame = detail::encodeControlFrame(detail::Opcode::Response, request_id, payload);
        }
        iterator->second.output.insert(iterator->second.output.end(), frame.begin(), frame.end());
        updateInterest(fd);
    }

    void writeClient(int fd) {
        auto iterator = clients.find(fd);
        if (iterator == clients.end()) {
            return;
        }
        ClientConnection& client = iterator->second;
        while (client.output_offset < client.output.size()) {
            const ssize_t sent =
                ::send(fd, client.output.data() + client.output_offset,
                       client.output.size() - client.output_offset, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent > 0) {
                client.output_offset += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent < 0 && errno == EINTR) {
                continue;
            }
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return;
            }
            removeClient(fd);
            return;
        }
        client.output.clear();
        client.output_offset = 0;
        if (client.close_after_write) {
            removeClient(fd);
            return;
        }
        updateInterest(fd);
    }

    void updateInterest(int fd) {
        const auto iterator = clients.find(fd);
        if (iterator == clients.end()) {
            return;
        }
        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP;
        if (iterator->second.output_offset < iterator->second.output.size()) {
            event.events |= EPOLLOUT;
        }
        event.data.fd = fd;
        if (::epoll_ctl(epoll_fd.get(), EPOLL_CTL_MOD, fd, &event) < 0) {
            removeClient(fd);
        }
    }

    void removeClient(int fd) {
        (void)::epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
        for (auto iterator = pending.begin(); iterator != pending.end();) {
            if (iterator->second.fd == fd) {
                iterator = pending.erase(iterator);
            } else {
                ++iterator;
            }
        }
        clients.erase(fd);
    }

    std::string socket_path;
    detail::UnixListener listener;
    detail::UniqueFd epoll_fd;
    RegistryState state;
    ConnectionId next_connection_id{1};
    std::map<int, ClientConnection> clients;
    std::map<std::uint64_t, PendingDiscovery> pending;
};

RegistryServer::RegistryServer(std::string socket_path)
    : impl_(std::make_unique<Impl>(std::move(socket_path))) {}

RegistryServer::~RegistryServer() = default;

void RegistryServer::pollOnce(int timeout_ms) { impl_->pollOnce(timeout_ms); }

const std::string& RegistryServer::socketPath() const noexcept { return impl_->socket_path; }

} // namespace mw::registry
