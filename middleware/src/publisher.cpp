#include <mw/publisher.hpp>

#include <mw/config.hpp>

#include "detail/frame_protocol.hpp"
#include "detail/registry_client.hpp"
#include "detail/shared_memory.hpp"
#include "detail/shm_protocol.hpp"
#include "detail/socket_io.hpp"
#include "detail/unix_socket.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace mw {
namespace {

bool validTransport(TransportType transport) noexcept {
    return transport == TransportType::UnixDomainSocket || transport == TransportType::SharedMemory;
}

void validateConfig(const PublisherConfig& config, bool registry_mode) {
    if (!registry_mode && config.socket_path.empty()) {
        throw std::invalid_argument("publisher socket path must not be empty");
    }
    if (config.max_message_size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("publisher max_message_size exceeds the data protocol");
    }
    if (!validTransport(config.transport)) {
        throw std::invalid_argument("publisher transport is invalid");
    }
    if (!registry_mode && config.transport != TransportType::UnixDomainSocket) {
        throw std::invalid_argument("direct mode supports the UDS baseline only");
    }
    if (registry_mode && (config.type_name.empty() || config.type_hash.empty())) {
        throw std::invalid_argument("publisher type name and hash must not be empty");
    }
    if (config.transport == TransportType::SharedMemory && config.shm_ack_timeout.count() <= 0) {
        throw std::invalid_argument("SHM acknowledgement timeout must be positive");
    }
}

std::uint64_t monotonicTimestampNs() noexcept {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp).count());
}

ErrorCode mapIoError(detail::IoStatus status) noexcept {
    return status == detail::IoStatus::Closed ? ErrorCode::ConnectionLost : ErrorCode::IoError;
}

int remainingPollTimeout(std::chrono::steady_clock::time_point deadline) noexcept {
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

ErrorCode readAckWithDeadline(int fd, std::array<std::uint8_t, detail::kShmAckSize>& buffer,
                              std::chrono::milliseconds timeout) noexcept {
    std::size_t transferred = 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (transferred < buffer.size()) {
        pollfd descriptor{fd, POLLIN, 0};
        const int result = ::poll(&descriptor, 1, remainingPollTimeout(deadline));
        if (result == 0) {
            return ErrorCode::Timeout;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ErrorCode::IoError;
        }
        if ((descriptor.revents & POLLNVAL) != 0) {
            return ErrorCode::ConnectionLost;
        }
        const ssize_t received =
            ::recv(fd, buffer.data() + transferred, buffer.size() - transferred, MSG_DONTWAIT);
        if (received > 0) {
            transferred += static_cast<std::size_t>(received);
            continue;
        }
        if (received == 0) {
            return ErrorCode::ConnectionLost;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        if (errno == ECONNRESET || errno == ENOTCONN) {
            return ErrorCode::ConnectionLost;
        }
        return ErrorCode::IoError;
    }
    return ErrorCode::Ok;
}

} // namespace

struct Publisher::Impl {
    Impl(std::string topic_value, const PublisherConfig& config_value)
        : topic(std::move(topic_value)), config(config_value),
          socket(detail::connectUnixSocket(config.socket_path)),
          negotiated_max_message_size(config.max_message_size) {}

    Impl(std::string topic_value, const PublisherConfig& config_value,
         std::shared_ptr<detail::RegistrySession> registry_session_value)
        : topic(std::move(topic_value)), config(config_value),
          registry_session(std::move(registry_session_value)),
          negotiated_max_message_size(config.max_message_size) {
        const detail::RegistryEndpoint endpoint = registry_session->advertise(topic, config);
        topic_id = endpoint.topic_id;
        endpoint_id = endpoint.endpoint_id;
    }

    ~Impl() {
        if (registry_session && endpoint_id != 0U) {
            registry_session->unadvertise(endpoint_id);
        }
    }

    ErrorCode connectDiscoveredEndpoint() noexcept {
        if (socket) {
            return ErrorCode::Ok;
        }
        if (!registry_session || endpoint_id == 0U || topic_id == 0U) {
            return ErrorCode::InvalidState;
        }

        try {
            const detail::RegistryDiscovery discovery = registry_session->resolve(endpoint_id);
            if (discovery.transport != config.transport || discovery.topic_id != topic_id) {
                return ErrorCode::TransportMismatch;
            }
            socket = detail::connectUnixSocket(discovery.data_socket_path);
            negotiated_max_message_size =
                std::min(config.max_message_size, discovery.max_message_size);
            return ErrorCode::Ok;
        } catch (const MiddlewareError& error) {
            return error.code();
        } catch (const std::system_error&) {
            return ErrorCode::ConnectionLost;
        } catch (const std::exception&) {
            return ErrorCode::IoError;
        }
    }

    PublishResult publishUds(const void* data, std::size_t size, std::uint64_t next_sequence) {
        const detail::FrameHeader header{detail::kFrameMagic, static_cast<std::uint32_t>(size),
                                         next_sequence, monotonicTimestampNs()};
        const auto encoded_header = detail::encodeFrameHeader(header);
        const detail::IoResult header_result =
            detail::writeAll(socket.get(), encoded_header.data(), encoded_header.size());
        if (header_result.status != detail::IoStatus::Complete) {
            const ErrorCode error = mapIoError(header_result.status);
            socket.reset();
            return {error, next_sequence, 0};
        }
        const detail::IoResult payload_result = detail::writeAll(socket.get(), data, size);
        if (payload_result.status != detail::IoStatus::Complete) {
            const ErrorCode error = mapIoError(payload_result.status);
            socket.reset();
            return {error, next_sequence, 0};
        }
        sequence = next_sequence;
        return {ErrorCode::Ok, next_sequence, size};
    }

    PublishResult publishSharedMemory(const void* data, std::size_t size,
                                      std::uint64_t next_sequence) {
        const std::size_t segment_size = detail::kSharedMemoryHeaderSize + size;
        const std::uint64_t timestamp = monotonicTimestampNs();
        const std::string name = "/mw_p3_" + std::to_string(::getpid()) + "_" +
                                 std::to_string(endpoint_id) + "_" + std::to_string(next_sequence);
        try {
            detail::SharedMemoryRegion region =
                detail::SharedMemoryRegion::create(name, segment_size);
            const detail::SharedMemoryHeader header{
                detail::kSharedMemoryMagic,
                detail::kSharedMemoryVersion,
                static_cast<std::uint16_t>(detail::kSharedMemoryHeaderSize),
                segment_size,
                size,
                next_sequence,
                timestamp,
                topic_id,
            };
            const auto encoded_header = detail::encodeSharedMemoryHeader(header);
            std::memcpy(region.data(), encoded_header.data(), encoded_header.size());
            if (size != 0U) {
                auto* payload_destination =
                    static_cast<std::uint8_t*>(region.data()) + detail::kSharedMemoryHeaderSize;
                std::memcpy(payload_destination, data, size);
            }

            detail::ShmNotification notification;
            notification.handle = {name, segment_size, size, next_sequence, timestamp, topic_id};
            const auto encoded_notification = detail::encodeShmNotification(notification);
            if (!encoded_notification.has_value()) {
                return {ErrorCode::InvalidState, next_sequence, 0};
            }
            const detail::IoResult notify_result = detail::writeAll(
                socket.get(), encoded_notification->data(), encoded_notification->size());
            if (notify_result.status != detail::IoStatus::Complete) {
                const ErrorCode error = mapIoError(notify_result.status);
                socket.reset();
                return {error, next_sequence, 0};
            }

            std::array<std::uint8_t, detail::kShmAckSize> encoded_ack{};
            const ErrorCode ack_read =
                readAckWithDeadline(socket.get(), encoded_ack, config.shm_ack_timeout);
            if (ack_read != ErrorCode::Ok) {
                socket.reset();
                return {ack_read, next_sequence, 0};
            }
            const auto ack = detail::decodeShmAck(encoded_ack.data(), encoded_ack.size());
            if (!ack.has_value() ||
                detail::validateShmAck(*ack, next_sequence) != detail::ShmValidation::Valid) {
                socket.reset();
                return {ErrorCode::InvalidFrame, next_sequence, 0};
            }

            sequence = next_sequence;
            if (!region.unlinkName()) {
                return {ErrorCode::CleanupFailed, next_sequence, size};
            }
            return {ErrorCode::Ok, next_sequence, size};
        } catch (const std::system_error&) {
            return {ErrorCode::SharedMemoryError, next_sequence, 0};
        } catch (const std::exception&) {
            return {ErrorCode::SharedMemoryError, next_sequence, 0};
        }
    }

    std::string topic;
    PublisherConfig config;
    detail::UniqueFd socket;
    std::shared_ptr<detail::RegistrySession> registry_session;
    std::uint64_t topic_id{0};
    std::uint64_t endpoint_id{0};
    std::size_t negotiated_max_message_size{0};
    std::uint64_t sequence{0};
};

Publisher::Publisher(std::string topic, const PublisherConfig& config) {
    validateConfig(config, false);
    impl_ = std::make_unique<Impl>(std::move(topic), config);
}

Publisher::Publisher(std::string topic, const PublisherConfig& config,
                     std::shared_ptr<detail::RegistrySession> registry_session) {
    validateConfig(config, true);
    impl_ = std::make_unique<Impl>(std::move(topic), config, std::move(registry_session));
}

Publisher::~Publisher() = default;
Publisher::Publisher(Publisher&&) noexcept = default;
Publisher& Publisher::operator=(Publisher&&) noexcept = default;

PublishResult Publisher::publish(const void* data, std::size_t size) {
    if (!impl_) {
        return {ErrorCode::InvalidState, 0, 0};
    }
    if (data == nullptr && size != 0U) {
        return {ErrorCode::InvalidArgument, 0, 0};
    }
    if (size > impl_->config.max_message_size || size > std::numeric_limits<std::uint32_t>::max()) {
        return {ErrorCode::MessageTooLarge, 0, 0};
    }
    if (impl_->sequence == std::numeric_limits<std::uint64_t>::max()) {
        return {ErrorCode::InvalidState, 0, 0};
    }

    const ErrorCode connection_result = impl_->connectDiscoveredEndpoint();
    if (connection_result != ErrorCode::Ok) {
        return {connection_result, 0, 0};
    }
    if (size > impl_->negotiated_max_message_size) {
        return {ErrorCode::MessageTooLarge, 0, 0};
    }

    const std::uint64_t next_sequence = impl_->sequence + 1U;
    if (impl_->config.transport == TransportType::SharedMemory) {
        return impl_->publishSharedMemory(data, size, next_sequence);
    }
    return impl_->publishUds(data, size, next_sequence);
}

const std::string& Publisher::topic() const noexcept {
    static const std::string empty;
    return impl_ ? impl_->topic : empty;
}

const char* transportTypeName(TransportType transport) noexcept {
    switch (transport) {
    case TransportType::UnixDomainSocket:
        return "uds";
    case TransportType::SharedMemory:
        return "shm";
    }
    return "unknown";
}

const char* errorMessage(ErrorCode error) noexcept {
    switch (error) {
    case ErrorCode::Ok:
        return "ok";
    case ErrorCode::InvalidArgument:
        return "invalid argument";
    case ErrorCode::InvalidState:
        return "invalid state";
    case ErrorCode::MessageTooLarge:
        return "message too large";
    case ErrorCode::ConnectionLost:
        return "connection lost";
    case ErrorCode::IoError:
        return "I/O error";
    case ErrorCode::Timeout:
        return "timeout";
    case ErrorCode::InvalidFrame:
        return "invalid frame";
    case ErrorCode::RegistryUnavailable:
        return "registry unavailable";
    case ErrorCode::DuplicateNode:
        return "duplicate node";
    case ErrorCode::DuplicatePublisher:
        return "duplicate publisher";
    case ErrorCode::TypeMismatch:
        return "type mismatch";
    case ErrorCode::TopicNotFound:
        return "topic not found";
    case ErrorCode::InvalidControlMessage:
        return "invalid control message";
    case ErrorCode::UnsupportedProtocolVersion:
        return "unsupported protocol version";
    case ErrorCode::UnknownOpcode:
        return "unknown opcode";
    case ErrorCode::InvalidRequestId:
        return "invalid request ID";
    case ErrorCode::NotRegistered:
        return "not registered";
    case ErrorCode::EndpointNotFound:
        return "endpoint not found";
    case ErrorCode::TransportMismatch:
        return "transport mismatch";
    case ErrorCode::SharedMemoryError:
        return "shared memory error";
    case ErrorCode::SharedMemoryNotFound:
        return "shared memory object not found";
    case ErrorCode::InvalidSharedMemory:
        return "invalid shared memory object";
    case ErrorCode::CleanupFailed:
        return "resource cleanup failed";
    }
    return "unknown error";
}

MiddlewareError::MiddlewareError(ErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

} // namespace mw
