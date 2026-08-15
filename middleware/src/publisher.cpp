#include <mw/publisher.hpp>

#include <mw/config.hpp>

#include "detail/frame_protocol.hpp"
#include "detail/memory_pool.hpp"
#include "detail/pool_protocol.hpp"
#include "detail/registry_client.hpp"
#include "detail/socket_io.hpp"
#include "detail/unix_socket.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

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
        throw std::invalid_argument("SHM release timeout must be positive");
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

std::uint64_t nextPoolId() noexcept {
    static std::atomic<std::uint32_t> counter{1U};
    const std::uint64_t process = static_cast<std::uint32_t>(::getpid());
    const std::uint64_t value = counter.fetch_add(1U, std::memory_order_relaxed);
    return (process << 32U) | value;
}

} // namespace

struct Publisher::Impl {
    struct DataConnection {
        std::uint64_t endpoint_id{0};
        detail::UniqueFd socket;
        std::size_t max_message_size{0};
        std::array<std::uint8_t, detail::kPoolReleaseSize> release_buffer{};
        std::size_t release_bytes{0};
    };

    Impl(std::string topic_value, const PublisherConfig& config_value)
        : topic(std::move(topic_value)), config(config_value),
          negotiated_max_message_size(config.max_message_size) {
        connections.push_back(
            {0U, detail::connectUnixSocket(config.socket_path), config.max_message_size, {}, 0U});
    }

    Impl(std::string topic_value, const PublisherConfig& config_value,
         std::shared_ptr<detail::RegistrySession> registry_session_value)
        : topic(std::move(topic_value)), config(config_value),
          registry_session(std::move(registry_session_value)),
          negotiated_max_message_size(config.max_message_size) {
        detail::PoolDescriptor advertised_pool;
        if (config.transport == TransportType::SharedMemory) {
            advertised_pool.pool_id = nextPoolId();
            advertised_pool.shm_name = "/mw_p4_" + std::to_string(::getpid()) + "_" +
                                       std::to_string(advertised_pool.pool_id);
            advertised_pool.segment_size =
                detail::MemoryPool::requiredSegmentSize(config.memory_pool);
            advertised_pool.layout_version = detail::kPoolLayoutVersion;
        }

        const detail::RegistryEndpoint endpoint =
            registry_session->advertise(topic, config, advertised_pool);
        topic_id = endpoint.topic_id;
        endpoint_id = endpoint.endpoint_id;
        if (config.transport == TransportType::SharedMemory) {
            advertised_pool.topic_id = topic_id;
            try {
                pool = detail::MemoryPool::create(advertised_pool, config.memory_pool);
            } catch (...) {
                registry_session->unadvertise(endpoint_id);
                endpoint_id = 0U;
                throw;
            }
        }
    }

    ~Impl() {
        connections.clear();
        pool.reset();
        if (registry_session && endpoint_id != 0U) {
            registry_session->unadvertise(endpoint_id);
        }
    }

    ErrorCode connectDiscoveredEndpoints() noexcept {
        if (!connections.empty()) {
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
            if (config.transport == TransportType::SharedMemory &&
                (!pool || discovery.pool.shm_name != pool->descriptor().shm_name ||
                 discovery.pool.pool_id != pool->descriptor().pool_id ||
                 discovery.pool.segment_size != pool->descriptor().segment_size ||
                 discovery.pool.layout_version != pool->descriptor().layout_version)) {
                return ErrorCode::InvalidSharedMemory;
            }

            const std::size_t connection_count =
                config.transport == TransportType::SharedMemory ? discovery.subscribers.size() : 1U;
            for (std::size_t index = 0; index < connection_count; ++index) {
                const detail::RegistrySubscriber& subscriber = discovery.subscribers[index];
                connections.push_back({subscriber.endpoint_id,
                                       detail::connectUnixSocket(subscriber.data_socket_path),
                                       subscriber.max_message_size,
                                       {},
                                       0U});
                negotiated_max_message_size =
                    std::min(negotiated_max_message_size, subscriber.max_message_size);
            }
            return connections.empty() ? ErrorCode::ConnectionLost : ErrorCode::Ok;
        } catch (const MiddlewareError& error) {
            connections.clear();
            return error.code();
        } catch (const std::system_error&) {
            connections.clear();
            return ErrorCode::ConnectionLost;
        } catch (const std::exception&) {
            connections.clear();
            return ErrorCode::IoError;
        }
    }

    PublishResult publishUds(const void* data, std::size_t size, std::uint64_t next_sequence) {
        DataConnection& connection = connections.front();
        const detail::FrameHeader header{detail::kFrameMagic, static_cast<std::uint32_t>(size),
                                         next_sequence, monotonicTimestampNs()};
        const auto encoded_header = detail::encodeFrameHeader(header);
        const detail::IoResult header_result =
            detail::writeAll(connection.socket.get(), encoded_header.data(), encoded_header.size());
        if (header_result.status != detail::IoStatus::Complete) {
            const ErrorCode error = mapIoError(header_result.status);
            connections.clear();
            return {error, next_sequence, 0};
        }
        const detail::IoResult payload_result =
            detail::writeAll(connection.socket.get(), data, size);
        if (payload_result.status != detail::IoStatus::Complete) {
            const ErrorCode error = mapIoError(payload_result.status);
            connections.clear();
            return {error, next_sequence, 0};
        }
        sequence = next_sequence;
        return {ErrorCode::Ok, next_sequence, size};
    }

    ErrorCode releaseReference(const detail::ChunkHandle& handle) {
        const detail::ReleaseResult result = pool->release(handle);
        if (result.error != ErrorCode::Ok) {
            return result.error;
        }
        if (result.became_released) {
            return pool->reclaim(handle);
        }
        return ErrorCode::Ok;
    }

    ErrorCode waitForReleases(const detail::ChunkHandle& handle, std::uint64_t expected_sequence,
                              std::map<std::uint64_t, bool>& outstanding) {
        const auto deadline = std::chrono::steady_clock::now() + config.shm_ack_timeout;
        ErrorCode first_error = ErrorCode::Ok;
        while (!outstanding.empty()) {
            std::vector<pollfd> descriptors;
            std::vector<std::size_t> connection_indices;
            descriptors.reserve(outstanding.size());
            connection_indices.reserve(outstanding.size());
            for (std::size_t index = 0; index < connections.size(); ++index) {
                if (outstanding.count(connections[index].endpoint_id) != 0U) {
                    descriptors.push_back({connections[index].socket.get(), POLLIN, 0});
                    connection_indices.push_back(index);
                }
            }
            if (descriptors.empty()) {
                return first_error == ErrorCode::Ok ? ErrorCode::ConnectionLost : first_error;
            }

            int poll_result = 0;
            do {
                poll_result =
                    ::poll(descriptors.data(), descriptors.size(), remainingPollTimeout(deadline));
            } while (poll_result < 0 && errno == EINTR);
            if (poll_result == 0) {
                return ErrorCode::Timeout;
            }
            if (poll_result < 0) {
                return ErrorCode::IoError;
            }

            std::vector<std::uint64_t> failed_endpoints;
            for (std::size_t descriptor_index = 0; descriptor_index < descriptors.size();
                 ++descriptor_index) {
                pollfd& descriptor = descriptors[descriptor_index];
                if (descriptor.revents == 0) {
                    continue;
                }
                DataConnection& connection = connections[connection_indices[descriptor_index]];
                bool failed = (descriptor.revents & POLLNVAL) != 0;
                while (!failed && connection.release_bytes < connection.release_buffer.size()) {
                    const ssize_t received = ::recv(
                        connection.socket.get(),
                        connection.release_buffer.data() + connection.release_bytes,
                        connection.release_buffer.size() - connection.release_bytes, MSG_DONTWAIT);
                    if (received > 0) {
                        connection.release_bytes += static_cast<std::size_t>(received);
                        continue;
                    }
                    if (received == 0) {
                        failed = true;
                    } else if (errno == EINTR) {
                        continue;
                    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        failed = true;
                    }
                    break;
                }

                if (!failed && connection.release_bytes == connection.release_buffer.size()) {
                    const auto release = detail::decodePoolRelease(
                        connection.release_buffer.data(), connection.release_buffer.size());
                    connection.release_bytes = 0U;
                    if (!release.has_value() ||
                        detail::validatePoolRelease(*release, handle, expected_sequence) !=
                            ErrorCode::Ok) {
                        failed = true;
                        if (first_error == ErrorCode::Ok) {
                            first_error = ErrorCode::InvalidChunkHandle;
                        }
                    } else {
                        const ErrorCode release_error = releaseReference(handle);
                        if (release_error != ErrorCode::Ok && first_error == ErrorCode::Ok) {
                            first_error = release_error;
                        }
                        outstanding.erase(connection.endpoint_id);
                    }
                }

                if (failed) {
                    const ErrorCode release_error = releaseReference(handle);
                    if (first_error == ErrorCode::Ok) {
                        first_error = release_error == ErrorCode::Ok ? ErrorCode::ConnectionLost
                                                                     : release_error;
                    }
                    outstanding.erase(connection.endpoint_id);
                    failed_endpoints.push_back(connection.endpoint_id);
                }
            }
            connections.erase(std::remove_if(connections.begin(), connections.end(),
                                             [&failed_endpoints](const DataConnection& connection) {
                                                 return std::find(failed_endpoints.begin(),
                                                                  failed_endpoints.end(),
                                                                  connection.endpoint_id) !=
                                                        failed_endpoints.end();
                                             }),
                              connections.end());
        }
        return first_error;
    }

    PublishResult publishSharedMemory(const void* data, std::size_t size,
                                      std::uint64_t next_sequence) {
        if (!pool || connections.empty()) {
            return {ErrorCode::InvalidState, next_sequence, 0};
        }
        const detail::AllocationResult allocation = pool->allocate(size);
        if (allocation.error != ErrorCode::Ok) {
            return {allocation.error, next_sequence, 0};
        }

        const std::uint64_t timestamp = monotonicTimestampNs();
        const ErrorCode publish_error =
            pool->writeAndPublish(allocation.handle, data, size, next_sequence, timestamp,
                                  static_cast<std::uint32_t>(connections.size()));
        if (publish_error != ErrorCode::Ok) {
            (void)pool->cancel(allocation.handle);
            return {publish_error, next_sequence, 0};
        }

        const detail::PoolNotification notification{pool->descriptor(), allocation.handle, size,
                                                    next_sequence, timestamp};
        const auto encoded_notification = detail::encodePoolNotification(notification);
        if (!encoded_notification.has_value()) {
            for (std::size_t index = 0; index < connections.size(); ++index) {
                (void)releaseReference(allocation.handle);
            }
            return {ErrorCode::InvalidState, next_sequence, 0};
        }

        std::map<std::uint64_t, bool> outstanding;
        std::vector<std::uint64_t> failed_endpoints;
        ErrorCode first_error = ErrorCode::Ok;
        for (DataConnection& connection : connections) {
            const detail::IoResult notify_result =
                detail::writeAll(connection.socket.get(), encoded_notification->data(),
                                 encoded_notification->size());
            if (notify_result.status == detail::IoStatus::Complete) {
                outstanding.emplace(connection.endpoint_id, true);
                continue;
            }
            const ErrorCode release_error = releaseReference(allocation.handle);
            first_error =
                release_error == ErrorCode::Ok ? mapIoError(notify_result.status) : release_error;
            failed_endpoints.push_back(connection.endpoint_id);
        }
        connections.erase(
            std::remove_if(connections.begin(), connections.end(),
                           [&failed_endpoints](const DataConnection& connection) {
                               return std::find(failed_endpoints.begin(), failed_endpoints.end(),
                                                connection.endpoint_id) != failed_endpoints.end();
                           }),
            connections.end());
        if (outstanding.empty()) {
            return {first_error == ErrorCode::Ok ? ErrorCode::ConnectionLost : first_error,
                    next_sequence, 0};
        }

        const ErrorCode release_error =
            waitForReleases(allocation.handle, next_sequence, outstanding);
        if (release_error != ErrorCode::Ok) {
            return {release_error, next_sequence, 0};
        }
        if (first_error != ErrorCode::Ok) {
            return {first_error, next_sequence, 0};
        }
        sequence = next_sequence;
        return {ErrorCode::Ok, next_sequence, size};
    }

    std::string topic;
    PublisherConfig config;
    std::vector<DataConnection> connections;
    std::shared_ptr<detail::RegistrySession> registry_session;
    std::unique_ptr<detail::MemoryPool> pool;
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

    const ErrorCode connection_result = impl_->connectDiscoveredEndpoints();
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
    case ErrorCode::PoolExhausted:
        return "memory pool exhausted";
    case ErrorCode::InvalidChunkHandle:
        return "invalid chunk handle";
    case ErrorCode::DuplicateRelease:
        return "duplicate chunk release";
    }
    return "unknown error";
}

MiddlewareError::MiddlewareError(ErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

} // namespace mw
