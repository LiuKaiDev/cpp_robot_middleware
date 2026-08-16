#include <mw/subscriber.hpp>

#include <mw/config.hpp>

#include "detail/frame_protocol.hpp"
#include "detail/memory_pool.hpp"
#include "detail/queue_protocol.hpp"
#include "detail/registry_client.hpp"
#include "detail/sample_release_context.hpp"
#include "detail/socket_io.hpp"
#include "detail/subscriber_queue.hpp"
#include "detail/unique_fd.hpp"
#include "detail/unix_socket.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
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

void validateConfig(const SubscriberConfig& config, bool registry_mode) {
    if (config.socket_path.empty()) {
        throw std::invalid_argument("subscriber socket path must not be empty");
    }
    if (config.max_message_size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("subscriber max_message_size exceeds the data protocol");
    }
    if (!validTransport(config.transport)) {
        throw std::invalid_argument("subscriber transport is invalid");
    }
    if (!registry_mode && config.transport != TransportType::UnixDomainSocket) {
        throw std::invalid_argument("direct mode supports the UDS baseline only");
    }
    if (registry_mode && (config.type_name.empty() || config.type_hash.empty())) {
        throw std::invalid_argument("subscriber type name and hash must not be empty");
    }
    if (config.queue_depth == 0U || config.queue_depth > detail::kMaxSubscriberQueueDepth) {
        throw std::invalid_argument("subscriber queue depth is outside the supported range");
    }
    if (!detail::validOverflowPolicy(config.overflow_policy)) {
        throw std::invalid_argument("subscriber overflow policy is invalid");
    }
    if (config.block_timeout.count() <= 0) {
        throw std::invalid_argument("subscriber block timeout must be positive");
    }
}

int pollTimeout(std::chrono::steady_clock::time_point deadline,
                std::chrono::milliseconds requested_timeout) noexcept {
    if (requested_timeout.count() <= 0) {
        return 0;
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

std::uint64_t nextQueueId() noexcept {
    static std::atomic<std::uint32_t> counter{1U};
    const std::uint64_t process = static_cast<std::uint32_t>(::getpid());
    const std::uint64_t value = counter.fetch_add(1U, std::memory_order_relaxed);
    return (process << 32U) | value;
}

class SocketReleaseContext final : public detail::SampleReleaseContext {
  public:
    explicit SocketReleaseContext(detail::UniqueFd socket) noexcept : socket_(std::move(socket)) {}

    int fd() const noexcept { return socket_.get(); }

    void release(const detail::ChunkHandle& handle) noexcept override {
        const auto frame = detail::encodeQueueRelease(handle);
        std::lock_guard<std::mutex> lock{write_mutex_};
        if (socket_) {
            (void)detail::writeAll(socket_.get(), frame.data(), frame.size());
        }
    }

  private:
    detail::UniqueFd socket_;
    std::mutex write_mutex_;
};

enum class UdsReceiveStatus {
    NeedMore,
    MessageReady,
    Disconnected,
    Truncated,
    InvalidFrame,
    MessageTooLarge,
    TransportMismatch,
    IoError,
};

struct UdsReceiveResult {
    UdsReceiveStatus status{UdsReceiveStatus::NeedMore};
    std::optional<ReceivedMessage> message;
};

} // namespace

struct Subscriber::Impl {
    struct ViewData {
        std::shared_ptr<detail::SampleReleaseContext> release_context;
        std::shared_ptr<detail::MemoryPoolView> pool_view;
        detail::ChunkHandle handle;
        const void* payload{nullptr};
        std::size_t payload_size{0};
        std::uint64_t sequence{0};
        std::uint64_t publish_timestamp_ns{0};
    };

    Impl(std::string topic_value, const SubscriberConfig& config_value)
        : topic(std::move(topic_value)), config(config_value),
          listener(detail::UnixListener::create(config.socket_path)) {}

    Impl(std::string topic_value, const SubscriberConfig& config_value,
         std::shared_ptr<detail::RegistrySession> registry_session_value)
        : topic(std::move(topic_value)), config(config_value),
          listener(detail::UnixListener::create(config.socket_path)),
          registry_session(std::move(registry_session_value)) {
        detail::QueueDescriptor advertised_queue;
        if (config.transport == TransportType::SharedMemory) {
            advertised_queue.queue_id = nextQueueId();
            advertised_queue.shm_name = "/mw_queue_" + std::to_string(::getpid()) + "_" +
                                        std::to_string(advertised_queue.queue_id);
            advertised_queue.capacity = config.queue_depth;
            advertised_queue.layout_version = detail::kQueueLayoutVersion;
            advertised_queue.overflow_policy = config.overflow_policy;
            advertised_queue.block_timeout_ms =
                static_cast<std::uint64_t>(config.block_timeout.count());
            advertised_queue.segment_size =
                detail::SharedSubscriberQueue::requiredSegmentSize(config.queue_depth);
            queue = detail::SharedSubscriberQueue::create(advertised_queue);
        }

        try {
            const detail::RegistryEndpoint endpoint =
                registry_session->subscribe(topic, config, advertised_queue);
            topic_id = endpoint.topic_id;
            endpoint_id = endpoint.endpoint_id;
            discovered_pool = endpoint.pool;
        } catch (...) {
            queue.reset();
            throw;
        }
    }

    ~Impl() {
        acceptPendingConnection();
        if (queue) {
            try {
                const auto handles = queue->closeAndDrain();
                if (release_context) {
                    for (const detail::ChunkHandle& handle : handles) {
                        release_context->release(handle);
                    }
                }
            } catch (...) {
            }
        }
        if (registry_session && endpoint_id != 0U) {
            registry_session->unsubscribe(endpoint_id);
        }
    }

    void acceptPendingConnection() noexcept {
        if (release_context) {
            return;
        }
        try {
            auto accepted = listener.accept();
            if (accepted.has_value()) {
                release_context = std::make_shared<SocketReleaseContext>(std::move(*accepted));
                resetFrame();
            }
        } catch (...) {
        }
    }

    int connectionFd() const noexcept { return release_context ? release_context->fd() : -1; }

    UdsReceiveResult receiveUdsAvailable() {
        while (true) {
            if (header_bytes < detail::kFrameHeaderSize) {
                const ssize_t received =
                    ::recv(connectionFd(), header_buffer.data() + header_bytes,
                           detail::kFrameHeaderSize - header_bytes, MSG_DONTWAIT);
                if (received > 0) {
                    header_bytes += static_cast<std::size_t>(received);
                    continue;
                }
                if (received == 0) {
                    return {header_bytes == 0U ? UdsReceiveStatus::Disconnected
                                               : UdsReceiveStatus::Truncated,
                            std::nullopt};
                }
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return {UdsReceiveStatus::NeedMore, std::nullopt};
                }
                if (errno == ECONNRESET || errno == ENOTCONN) {
                    return {UdsReceiveStatus::Disconnected, std::nullopt};
                }
                return {UdsReceiveStatus::IoError, std::nullopt};
            }

            if (!decoded_header.has_value()) {
                decoded_header =
                    detail::decodeFrameHeader(header_buffer.data(), detail::kFrameHeaderSize);
                if (!decoded_header.has_value()) {
                    return {UdsReceiveStatus::InvalidFrame, std::nullopt};
                }
                const detail::FrameValidation validation =
                    detail::validateFrameHeader(*decoded_header, config.max_message_size);
                if (validation == detail::FrameValidation::BadMagic) {
                    return {decoded_header->magic == detail::kQueueProtocolMagic
                                ? UdsReceiveStatus::TransportMismatch
                                : UdsReceiveStatus::InvalidFrame,
                            std::nullopt};
                }
                if (validation == detail::FrameValidation::PayloadTooLarge) {
                    return {UdsReceiveStatus::MessageTooLarge, std::nullopt};
                }
                payload.resize(decoded_header->payload_size);
            }

            while (payload_bytes < payload.size()) {
                const ssize_t received = ::recv(connectionFd(), payload.data() + payload_bytes,
                                                payload.size() - payload_bytes, MSG_DONTWAIT);
                if (received > 0) {
                    payload_bytes += static_cast<std::size_t>(received);
                    continue;
                }
                if (received == 0) {
                    return {UdsReceiveStatus::Truncated, std::nullopt};
                }
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return {UdsReceiveStatus::NeedMore, std::nullopt};
                }
                return {UdsReceiveStatus::IoError, std::nullopt};
            }

            ReceivedMessage message{std::move(payload), decoded_header->sequence,
                                    decoded_header->timestamp_ns};
            resetFrame();
            return {UdsReceiveStatus::MessageReady, std::move(message)};
        }
    }

    ErrorCode receiveWakeAvailable() {
        while (header_bytes < detail::kQueueWakeSize) {
            const ssize_t received = ::recv(connectionFd(), header_buffer.data() + header_bytes,
                                            detail::kQueueWakeSize - header_bytes, MSG_DONTWAIT);
            if (received > 0) {
                header_bytes += static_cast<std::size_t>(received);
                continue;
            }
            if (received == 0) {
                return header_bytes == 0U ? ErrorCode::ConnectionLost : ErrorCode::InvalidFrame;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return ErrorCode::Timeout;
            }
            if (errno == ECONNRESET || errno == ENOTCONN) {
                return ErrorCode::ConnectionLost;
            }
            return ErrorCode::IoError;
        }

        const auto wake = detail::decodeQueueWake(header_buffer.data(), detail::kQueueWakeSize);
        resetFrame();
        if (!wake.has_value() || !queue ||
            detail::validateQueueWake(*wake, queue->descriptor().queue_id, topic_id) !=
                ErrorCode::Ok) {
            return ErrorCode::InvalidFrame;
        }
        if (!discovered_pool.shm_name.empty() &&
            (discovered_pool.shm_name != wake->pool.shm_name ||
             discovered_pool.pool_id != wake->pool.pool_id ||
             discovered_pool.segment_size != wake->pool.segment_size ||
             discovered_pool.layout_version != wake->pool.layout_version)) {
            return ErrorCode::InvalidSharedMemory;
        }
        try {
            if (!pool_view) {
                auto opened = detail::MemoryPoolView::open(wake->pool, topic_id);
                pool_view = std::shared_ptr<detail::MemoryPoolView>(std::move(opened));
                discovered_pool = wake->pool;
            } else if (pool_view->descriptor().shm_name != wake->pool.shm_name ||
                       pool_view->descriptor().pool_id != wake->pool.pool_id ||
                       pool_view->descriptor().segment_size != wake->pool.segment_size) {
                return ErrorCode::InvalidSharedMemory;
            }
        } catch (const MiddlewareError& error) {
            return error.code();
        } catch (const std::system_error& error) {
            return error.code().value() == ENOENT ? ErrorCode::SharedMemoryNotFound
                                                  : ErrorCode::SharedMemoryError;
        } catch (...) {
            return ErrorCode::SharedMemoryError;
        }
        return ErrorCode::Ok;
    }

    ErrorCode drainWakeNotificationsNonBlocking() {
        if (!release_context || !pool_view) {
            return ErrorCode::Ok;
        }
        while (true) {
            const ErrorCode error = receiveWakeAvailable();
            if (error == ErrorCode::Timeout) {
                return ErrorCode::Ok;
            }
            if (error != ErrorCode::Ok) {
                return error;
            }
        }
    }

    std::optional<ViewData> tryTakeQueued() {
        if (!queue || !pool_view || !release_context) {
            return std::nullopt;
        }
        const auto next = queue->peek();
        if (!next.has_value()) {
            return std::nullopt;
        }
        if (next->pool_id != pool_view->descriptor().pool_id) {
            // A replacement publisher may enqueue before the old data socket reports HUP.
            // Leave the new handle queued until its wake frame installs the matching pool.
            return std::nullopt;
        }
        const auto handle = queue->tryDequeue();
        if (!handle.has_value()) {
            return std::nullopt;
        }
        const detail::ChunkViewResult view = pool_view->read(*handle, config.max_message_size);
        if (view.error != ErrorCode::Ok) {
            release_context->release(*handle);
            last_error = view.error;
            return std::nullopt;
        }
        last_error = ErrorCode::Ok;
        return ViewData{release_context,
                        pool_view,
                        *handle,
                        view.payload,
                        view.payload_size,
                        view.sequence,
                        view.publish_timestamp_ns};
    }

    void resetConnection() noexcept {
        release_context.reset();
        resetFrame();
    }

    void resetPublisherState(std::uint64_t dead_pool_id = 0U) noexcept {
        if (queue) {
            queue->discardPool(dead_pool_id);
        }
        if (dead_pool_id != 0U && discovered_pool.pool_id != 0U &&
            discovered_pool.pool_id != dead_pool_id) {
            return;
        }
        resetConnection();
        pool_view.reset();
        discovered_pool = {};
    }

    void processPeerEvents() noexcept {
        if (!registry_session || endpoint_id == 0U) {
            return;
        }
        try {
            const auto events = registry_session->takePeerEvents(endpoint_id);
            for (const detail::RegistryPeerEvent& event : events) {
                if (event.kind == detail::PeerEventKind::PublisherDead) {
                    resetPublisherState(event.resource_id);
                }
            }
        } catch (...) {
        }
    }

    void resetFrame() noexcept {
        header_bytes = 0U;
        decoded_header.reset();
        payload.clear();
        payload_bytes = 0U;
    }

    std::string topic;
    SubscriberConfig config;
    detail::UnixListener listener;
    std::shared_ptr<SocketReleaseContext> release_context;
    std::array<std::uint8_t, detail::kQueueWakeSize> header_buffer{};
    std::size_t header_bytes{0};
    std::optional<detail::FrameHeader> decoded_header;
    std::vector<std::uint8_t> payload;
    std::size_t payload_bytes{0};
    ErrorCode last_error{ErrorCode::Ok};
    std::shared_ptr<detail::RegistrySession> registry_session;
    std::uint64_t topic_id{0};
    std::uint64_t endpoint_id{0};
    detail::PoolDescriptor discovered_pool;
    std::unique_ptr<detail::SharedSubscriberQueue> queue;
    std::shared_ptr<detail::MemoryPoolView> pool_view;
};

Subscriber::Subscriber(std::string topic, const SubscriberConfig& config) {
    validateConfig(config, false);
    impl_ = std::make_unique<Impl>(std::move(topic), config);
}

Subscriber::Subscriber(std::string topic, const SubscriberConfig& config,
                       std::shared_ptr<detail::RegistrySession> registry_session) {
    validateConfig(config, true);
    impl_ = std::make_unique<Impl>(std::move(topic), config, std::move(registry_session));
}

Subscriber::~Subscriber() = default;
Subscriber::Subscriber(Subscriber&&) noexcept = default;
Subscriber& Subscriber::operator=(Subscriber&&) noexcept = default;

std::optional<ReceivedMessage> Subscriber::take() {
    return waitAndTake(std::chrono::milliseconds{0});
}

std::optional<ReceivedMessage> Subscriber::waitAndTake(std::chrono::milliseconds timeout) {
    if (!impl_) {
        return std::nullopt;
    }
    if (impl_->config.transport == TransportType::SharedMemory) {
        auto view = waitAndTakeView(timeout);
        if (!view.has_value()) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> copied(view->size());
        if (!copied.empty()) {
            std::memcpy(copied.data(), view->data(), copied.size());
        }
        return ReceivedMessage{std::move(copied),    view->sequence(),   view->publishTimestampNs(),
                               view->poolId(),       view->chunkIndex(), view->generation(),
                               view->payloadOffset()};
    }

    impl_->processPeerEvents();

    timeout = std::max(timeout, std::chrono::milliseconds{0});
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (!impl_->release_context) {
            pollfd descriptor{impl_->listener.fd(), POLLIN, 0};
            const int result = ::poll(&descriptor, 1, pollTimeout(deadline, timeout));
            if (result == 0) {
                impl_->last_error = ErrorCode::Timeout;
                return std::nullopt;
            }
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                impl_->last_error = ErrorCode::IoError;
                return std::nullopt;
            }
            impl_->acceptPendingConnection();
            if (!impl_->release_context) {
                continue;
            }
        }

        pollfd descriptor{impl_->connectionFd(), POLLIN, 0};
        const int result = ::poll(&descriptor, 1, pollTimeout(deadline, timeout));
        if (result == 0) {
            impl_->last_error = ErrorCode::Timeout;
            return std::nullopt;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            impl_->last_error = ErrorCode::IoError;
            impl_->resetConnection();
            return std::nullopt;
        }
        if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
            continue;
        }

        UdsReceiveResult received = impl_->receiveUdsAvailable();
        switch (received.status) {
        case UdsReceiveStatus::NeedMore:
            continue;
        case UdsReceiveStatus::MessageReady:
            impl_->last_error = ErrorCode::Ok;
            return std::move(received.message);
        case UdsReceiveStatus::Disconnected:
            impl_->last_error = ErrorCode::ConnectionLost;
            break;
        case UdsReceiveStatus::Truncated:
        case UdsReceiveStatus::InvalidFrame:
            impl_->last_error = ErrorCode::InvalidFrame;
            break;
        case UdsReceiveStatus::MessageTooLarge:
            impl_->last_error = ErrorCode::MessageTooLarge;
            break;
        case UdsReceiveStatus::TransportMismatch:
            impl_->last_error = ErrorCode::TransportMismatch;
            break;
        case UdsReceiveStatus::IoError:
            impl_->last_error = ErrorCode::IoError;
            break;
        }
        impl_->resetConnection();
        return std::nullopt;
    }
}

std::optional<SampleView> Subscriber::takeView() {
    return waitAndTakeView(std::chrono::milliseconds{0});
}

std::optional<SampleView> Subscriber::waitAndTakeView(std::chrono::milliseconds timeout) {
    if (!impl_) {
        return std::nullopt;
    }
    if (impl_->config.transport != TransportType::SharedMemory) {
        impl_->last_error = ErrorCode::UnsupportedTransport;
        return std::nullopt;
    }

    impl_->processPeerEvents();
    impl_->last_error = ErrorCode::Ok;

    const ErrorCode pending_wake_error = impl_->drainWakeNotificationsNonBlocking();
    if (pending_wake_error != ErrorCode::Ok) {
        impl_->last_error = pending_wake_error;
        if (pending_wake_error == ErrorCode::ConnectionLost) {
            impl_->resetPublisherState(impl_->discovered_pool.pool_id);
        } else {
            impl_->resetConnection();
        }
        return std::nullopt;
    }

    timeout = std::max(timeout, std::chrono::milliseconds{0});
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (auto view = impl_->tryTakeQueued(); view.has_value()) {
            return SampleView{std::move(view->release_context),
                              std::move(view->pool_view),
                              view->payload,
                              view->payload_size,
                              view->sequence,
                              view->publish_timestamp_ns,
                              view->handle.pool_id,
                              view->handle.chunk_index,
                              view->handle.generation,
                              view->handle.payload_offset};
        }
        if (impl_->last_error != ErrorCode::Ok && impl_->last_error != ErrorCode::Timeout) {
            return std::nullopt;
        }

        if (!impl_->release_context) {
            pollfd descriptor{impl_->listener.fd(), POLLIN, 0};
            const int result = ::poll(&descriptor, 1, pollTimeout(deadline, timeout));
            if (result == 0) {
                impl_->last_error = ErrorCode::Timeout;
                return std::nullopt;
            }
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                impl_->last_error = ErrorCode::IoError;
                return std::nullopt;
            }
            impl_->acceptPendingConnection();
            if (!impl_->release_context) {
                continue;
            }
        }

        pollfd descriptor{impl_->connectionFd(), POLLIN, 0};
        const int result = ::poll(&descriptor, 1, pollTimeout(deadline, timeout));
        if (result == 0) {
            impl_->last_error = ErrorCode::Timeout;
            return std::nullopt;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            impl_->last_error = ErrorCode::IoError;
            impl_->resetConnection();
            return std::nullopt;
        }
        if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
            continue;
        }
        const ErrorCode wake_error = impl_->receiveWakeAvailable();
        if (wake_error == ErrorCode::Timeout) {
            continue;
        }
        if (wake_error != ErrorCode::Ok) {
            impl_->last_error = wake_error;
            if (wake_error == ErrorCode::ConnectionLost) {
                impl_->resetPublisherState(impl_->discovered_pool.pool_id);
            } else {
                impl_->resetConnection();
            }
            return std::nullopt;
        }
        impl_->last_error = ErrorCode::Ok;
    }
}

ErrorCode Subscriber::lastError() const noexcept {
    return impl_ ? impl_->last_error : ErrorCode::InvalidState;
}

const std::string& Subscriber::topic() const noexcept {
    static const std::string empty;
    return impl_ ? impl_->topic : empty;
}

} // namespace mw
