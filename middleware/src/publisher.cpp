#include <mw/publisher.hpp>

#include <mw/config.hpp>

#include "detail/frame_protocol.hpp"
#include "detail/loaned_sample_owner.hpp"
#include "detail/memory_pool.hpp"
#include "detail/queue_protocol.hpp"
#include "detail/registry_client.hpp"
#include "detail/socket_io.hpp"
#include "detail/subscriber_queue.hpp"
#include "detail/unix_socket.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
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

struct WakeContext {
    int fd{-1};
    std::array<std::uint8_t, detail::kQueueWakeSize> frame{};
};

bool sendWake(void* raw_context) noexcept {
    auto* context = static_cast<WakeContext*>(raw_context);
    const detail::IoResult result =
        detail::writeAll(context->fd, context->frame.data(), context->frame.size());
    return result.status == detail::IoStatus::Complete;
}

} // namespace

struct Publisher::Impl : detail::LoanedSampleOwner {
    struct DataConnection {
        std::uint64_t endpoint_id{0};
        detail::UniqueFd socket;
        std::size_t max_message_size{0};
        std::unique_ptr<detail::SharedSubscriberQueue> queue;
        std::array<std::uint8_t, detail::kQueueReleaseSize> release_buffer{};
        std::size_t release_bytes{0};
        std::vector<detail::ChunkHandle> outstanding;
    };

    struct LoanAllocation {
        detail::AllocationResult allocation;
        void* payload{nullptr};
    };

    Impl(std::string topic_value, const PublisherConfig& config_value)
        : topic(std::move(topic_value)), config(config_value),
          negotiated_max_message_size(config.max_message_size) {
        DataConnection connection;
        connection.socket = detail::connectUnixSocket(config.socket_path);
        connection.max_message_size = config.max_message_size;
        connections.push_back(std::move(connection));
    }

    Impl(std::string topic_value, const PublisherConfig& config_value,
         std::shared_ptr<detail::RegistrySession> registry_session_value)
        : topic(std::move(topic_value)), config(config_value),
          registry_session(std::move(registry_session_value)),
          negotiated_max_message_size(config.max_message_size) {
        detail::PoolDescriptor advertised_pool;
        if (config.transport == TransportType::SharedMemory) {
            advertised_pool.pool_id = nextPoolId();
            advertised_pool.shm_name = "/mw_p5_" + std::to_string(::getpid()) + "_" +
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

    ~Impl() override {
        if (pool) {
            waitForOutstandingReleases();
            drainQueuedReferences();
        }
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
                DataConnection connection;
                connection.endpoint_id = subscriber.endpoint_id;
                connection.socket = detail::connectUnixSocket(subscriber.data_socket_path);
                connection.max_message_size = subscriber.max_message_size;
                if (config.transport == TransportType::SharedMemory) {
                    connection.queue = detail::SharedSubscriberQueue::open(subscriber.queue);
                }
                negotiated_max_message_size =
                    std::min(negotiated_max_message_size, subscriber.max_message_size);
                connections.push_back(std::move(connection));
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

    ErrorCode releaseReference(const detail::ChunkHandle& handle) noexcept {
        if (!pool) {
            return ErrorCode::InvalidState;
        }
        const detail::ReleaseResult result = pool->release(handle);
        if (result.error != ErrorCode::Ok) {
            return result.error;
        }
        return result.became_released ? pool->reclaim(handle) : ErrorCode::Ok;
    }

    void releaseOutstanding(DataConnection& connection,
                            const detail::ChunkHandle& handle) noexcept {
        const auto item = std::find(connection.outstanding.begin(), connection.outstanding.end(),
                                    handle);
        if (item == connection.outstanding.end()) {
            return;
        }
        (void)releaseReference(handle);
        connection.outstanding.erase(item);
    }

    bool receiveOneRelease(DataConnection& connection) noexcept {
        while (connection.release_bytes < connection.release_buffer.size()) {
            const ssize_t received = ::recv(
                connection.socket.get(), connection.release_buffer.data() + connection.release_bytes,
                connection.release_buffer.size() - connection.release_bytes, MSG_DONTWAIT);
            if (received > 0) {
                connection.release_bytes += static_cast<std::size_t>(received);
                continue;
            }
            if (received == 0) {
                return false;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            return false;
        }

        const auto handle = detail::decodeQueueRelease(connection.release_buffer.data(),
                                                       connection.release_buffer.size());
        connection.release_bytes = 0U;
        if (!handle.has_value()) {
            return false;
        }
        releaseOutstanding(connection, *handle);
        return true;
    }

    void drainReleasesNonBlocking() noexcept {
        if (!pool) {
            return;
        }
        for (DataConnection& connection : connections) {
            while (true) {
                const std::size_t before = connection.release_bytes;
                const std::size_t outstanding_before = connection.outstanding.size();
                if (!receiveOneRelease(connection)) {
                    break;
                }
                if (before == connection.release_bytes &&
                    outstanding_before == connection.outstanding.size()) {
                    break;
                }
            }
        }
    }

    std::size_t outstandingCount() const noexcept {
        std::size_t count = 0U;
        for (const DataConnection& connection : connections) {
            count += connection.outstanding.size();
        }
        return count;
    }

    void waitForOutstandingReleases() noexcept {
        const auto deadline = std::chrono::steady_clock::now() + config.shm_ack_timeout;
        while (outstandingCount() != 0U) {
            std::vector<pollfd> descriptors;
            descriptors.reserve(connections.size());
            for (const DataConnection& connection : connections) {
                if (!connection.outstanding.empty()) {
                    descriptors.push_back({connection.socket.get(), POLLIN, 0});
                }
            }
            if (descriptors.empty()) {
                return;
            }
            int result = 0;
            do {
                result = ::poll(descriptors.data(), descriptors.size(),
                                remainingPollTimeout(deadline));
            } while (result < 0 && errno == EINTR);
            if (result <= 0) {
                return;
            }
            drainReleasesNonBlocking();
        }
    }

    void drainQueuedReferences() noexcept {
        for (DataConnection& connection : connections) {
            if (!connection.queue) {
                continue;
            }
            try {
                for (const detail::ChunkHandle& handle : connection.queue->drain()) {
                    releaseOutstanding(connection, handle);
                }
            } catch (...) {
            }
        }
    }

    PublishResult dispatchPublished(const detail::ChunkHandle& handle, std::size_t payload_size,
                                    std::uint64_t next_sequence) {
        PublishResult result{ErrorCode::ConnectionLost, next_sequence, payload_size};
        ErrorCode first_failure = ErrorCode::Ok;
        std::vector<std::uint64_t> failed_endpoints;

        for (DataConnection& connection : connections) {
            if (!connection.queue) {
                if (first_failure == ErrorCode::Ok) {
                    first_failure = ErrorCode::InvalidState;
                }
                continue;
            }
            const ErrorCode reference_error = pool->addReference(handle);
            if (reference_error != ErrorCode::Ok) {
                if (first_failure == ErrorCode::Ok) {
                    first_failure = reference_error;
                }
                continue;
            }

            const auto encoded_wake = detail::encodeQueueWake(
                {pool->descriptor(), connection.queue->descriptor().queue_id});
            if (!encoded_wake.has_value()) {
                (void)releaseReference(handle);
                if (first_failure == ErrorCode::Ok) {
                    first_failure = ErrorCode::InvalidState;
                }
                continue;
            }
            WakeContext wake{connection.socket.get(), *encoded_wake};
            const detail::QueueEnqueueResult enqueue =
                connection.queue->enqueue(handle, sendWake, &wake);
            if (enqueue.status == detail::QueueEnqueueStatus::Accepted) {
                connection.outstanding.push_back(handle);
                ++result.enqueued;
                if (enqueue.dropped_oldest.has_value()) {
                    ++result.dropped_oldest;
                    releaseOutstanding(connection, *enqueue.dropped_oldest);
                }
                continue;
            }

            (void)releaseReference(handle);
            switch (enqueue.status) {
            case detail::QueueEnqueueStatus::DroppedNewest:
                ++result.dropped_newest;
                if (first_failure == ErrorCode::Ok) {
                    first_failure = ErrorCode::QueueFull;
                }
                break;
            case detail::QueueEnqueueStatus::TimedOut:
                ++result.block_timeouts;
                first_failure = ErrorCode::QueueTimeout;
                break;
            case detail::QueueEnqueueStatus::Closed:
                if (first_failure == ErrorCode::Ok) {
                    first_failure = ErrorCode::QueueClosed;
                }
                failed_endpoints.push_back(connection.endpoint_id);
                break;
            case detail::QueueEnqueueStatus::NotificationFailed:
                if (first_failure == ErrorCode::Ok) {
                    first_failure = ErrorCode::ConnectionLost;
                }
                failed_endpoints.push_back(connection.endpoint_id);
                break;
            case detail::QueueEnqueueStatus::SynchronizationError:
                if (first_failure == ErrorCode::Ok) {
                    first_failure = ErrorCode::SharedMemoryError;
                }
                break;
            case detail::QueueEnqueueStatus::Accepted:
                break;
            }
        }

        (void)releaseReference(handle); // Publisher guard reference.
        sequence = next_sequence;
        connections.erase(
            std::remove_if(connections.begin(), connections.end(),
                           [&failed_endpoints](const DataConnection& connection) {
                               return std::find(failed_endpoints.begin(), failed_endpoints.end(),
                                                connection.endpoint_id) != failed_endpoints.end();
                           }),
            connections.end());
        result.error = result.enqueued != 0U
                           ? ErrorCode::Ok
                           : (first_failure == ErrorCode::Ok ? ErrorCode::ConnectionLost
                                                             : first_failure);
        return result;
    }

    PublishResult publishSharedMemoryCopy(const void* data, std::size_t size,
                                          std::uint64_t next_sequence) {
        const detail::AllocationResult allocation = pool->allocate(size);
        if (allocation.error != ErrorCode::Ok) {
            return {allocation.error, next_sequence, 0};
        }
        const std::uint64_t timestamp = monotonicTimestampNs();
        const ErrorCode publish_error =
            pool->writeAndPublish(allocation.handle, data, size, next_sequence, timestamp, 1U);
        if (publish_error != ErrorCode::Ok) {
            (void)pool->cancel(allocation.handle);
            return {publish_error, next_sequence, 0};
        }
        return dispatchPublished(allocation.handle, size, next_sequence);
    }

    LoanAllocation allocateLoan(std::size_t size) noexcept {
        if (config.transport != TransportType::SharedMemory || !pool) {
            return {{ErrorCode::UnsupportedTransport, {}, 0U}, nullptr};
        }
        if (size > config.max_message_size || size > std::numeric_limits<std::uint32_t>::max()) {
            return {{ErrorCode::MessageTooLarge, {}, 0U}, nullptr};
        }
        drainReleasesNonBlocking();
        detail::AllocationResult allocation = pool->allocate(size);
        if (allocation.error != ErrorCode::Ok) {
            return {allocation, nullptr};
        }
        const detail::WritableChunkResult writable = pool->writablePayload(allocation.handle);
        if (writable.error != ErrorCode::Ok) {
            (void)pool->cancel(allocation.handle);
            allocation.error = writable.error;
            return {allocation, nullptr};
        }
        return {allocation, writable.payload};
    }

    PublishResult publishLoanedSample(const detail::ChunkHandle& handle,
                                      std::size_t payload_size) override {
        if (!pool || config.transport != TransportType::SharedMemory) {
            return {ErrorCode::UnsupportedTransport, 0U, 0U};
        }
        if (sequence == std::numeric_limits<std::uint64_t>::max()) {
            (void)pool->cancel(handle);
            return {ErrorCode::InvalidState, 0U, 0U};
        }
        drainReleasesNonBlocking();
        const ErrorCode connection_error = connectDiscoveredEndpoints();
        if (connection_error != ErrorCode::Ok) {
            (void)pool->cancel(handle);
            return {connection_error, 0U, 0U};
        }
        if (payload_size > negotiated_max_message_size) {
            (void)pool->cancel(handle);
            return {ErrorCode::MessageTooLarge, 0U, 0U};
        }

        const std::uint64_t next_sequence = sequence + 1U;
        const ErrorCode publish_error = pool->publishLoaned(
            handle, payload_size, next_sequence, monotonicTimestampNs(), 1U);
        if (publish_error != ErrorCode::Ok) {
            (void)pool->cancel(handle);
            return {publish_error, next_sequence, 0U};
        }
        return dispatchPublished(handle, payload_size, next_sequence);
    }

    ErrorCode cancelLoanedSample(const detail::ChunkHandle& handle) noexcept override {
        return pool ? pool->cancel(handle) : ErrorCode::InvalidState;
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
    impl_ = std::make_shared<Impl>(std::move(topic), config);
}

Publisher::Publisher(std::string topic, const PublisherConfig& config,
                     std::shared_ptr<detail::RegistrySession> registry_session) {
    validateConfig(config, true);
    impl_ = std::make_shared<Impl>(std::move(topic), config, std::move(registry_session));
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

    impl_->drainReleasesNonBlocking();
    const ErrorCode connection_result = impl_->connectDiscoveredEndpoints();
    if (connection_result != ErrorCode::Ok) {
        return {connection_result, 0, 0};
    }
    if (size > impl_->negotiated_max_message_size) {
        return {ErrorCode::MessageTooLarge, 0, 0};
    }

    const std::uint64_t next_sequence = impl_->sequence + 1U;
    if (impl_->config.transport == TransportType::SharedMemory) {
        return impl_->publishSharedMemoryCopy(data, size, next_sequence);
    }
    return impl_->publishUds(data, size, next_sequence);
}

LoanedSample Publisher::loan(std::size_t size) {
    if (!impl_) {
        return LoanedSample{ErrorCode::InvalidState};
    }
    const Impl::LoanAllocation loan = impl_->allocateLoan(size);
    if (loan.allocation.error != ErrorCode::Ok) {
        return LoanedSample{loan.allocation.error};
    }
    return LoanedSample{std::static_pointer_cast<detail::LoanedSampleOwner>(impl_), loan.payload,
                        size, loan.allocation.capacity, loan.allocation.handle.pool_id,
                        loan.allocation.handle.chunk_index, loan.allocation.handle.generation,
                        loan.allocation.handle.payload_offset};
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

const char* overflowPolicyName(OverflowPolicy policy) noexcept {
    switch (policy) {
    case OverflowPolicy::DropNewest:
        return "drop_newest";
    case OverflowPolicy::DropOldest:
        return "drop_oldest";
    case OverflowPolicy::BlockWithTimeout:
        return "block_with_timeout";
    }
    return "unknown";
}

const char* errorMessage(ErrorCode error) noexcept {
    switch (error) {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::InvalidArgument: return "invalid argument";
    case ErrorCode::InvalidState: return "invalid state";
    case ErrorCode::MessageTooLarge: return "message too large";
    case ErrorCode::ConnectionLost: return "connection lost";
    case ErrorCode::IoError: return "I/O error";
    case ErrorCode::Timeout: return "timeout";
    case ErrorCode::InvalidFrame: return "invalid frame";
    case ErrorCode::RegistryUnavailable: return "registry unavailable";
    case ErrorCode::DuplicateNode: return "duplicate node";
    case ErrorCode::DuplicatePublisher: return "duplicate publisher";
    case ErrorCode::TypeMismatch: return "type mismatch";
    case ErrorCode::TopicNotFound: return "topic not found";
    case ErrorCode::InvalidControlMessage: return "invalid control message";
    case ErrorCode::UnsupportedProtocolVersion: return "unsupported protocol version";
    case ErrorCode::UnknownOpcode: return "unknown opcode";
    case ErrorCode::InvalidRequestId: return "invalid request ID";
    case ErrorCode::NotRegistered: return "not registered";
    case ErrorCode::EndpointNotFound: return "endpoint not found";
    case ErrorCode::TransportMismatch: return "transport mismatch";
    case ErrorCode::SharedMemoryError: return "shared memory error";
    case ErrorCode::SharedMemoryNotFound: return "shared memory object not found";
    case ErrorCode::InvalidSharedMemory: return "invalid shared memory object";
    case ErrorCode::CleanupFailed: return "resource cleanup failed";
    case ErrorCode::PoolExhausted: return "memory pool exhausted";
    case ErrorCode::InvalidChunkHandle: return "invalid chunk handle";
    case ErrorCode::DuplicateRelease: return "duplicate chunk release";
    case ErrorCode::QueueFull: return "subscriber queue full";
    case ErrorCode::QueueTimeout: return "subscriber queue wait timed out";
    case ErrorCode::QueueClosed: return "subscriber queue closed";
    case ErrorCode::UnsupportedTransport: return "operation is unsupported by this transport";
    }
    return "unknown error";
}

MiddlewareError::MiddlewareError(ErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

} // namespace mw
