#include "detail/subscriber_queue.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <time.h>
#include <type_traits>
#include <utility>

namespace mw::detail {
namespace {

bool checkedAdd(std::size_t left, std::size_t right, std::size_t& result) noexcept {
    if (left > std::numeric_limits<std::size_t>::max() - right) {
        return false;
    }
    result = left + right;
    return true;
}

bool checkedMultiply(std::size_t left, std::size_t right, std::size_t& result) noexcept {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

constexpr std::size_t slotsOffset() noexcept {
    return (sizeof(SubscriberQueueHeader) + alignof(ChunkHandle) - 1U) &
           ~(alignof(ChunkHandle) - 1U);
}

static_assert(std::is_trivially_copyable<ChunkHandle>::value,
              "queue entries must be trivially copyable");
static_assert(sizeof(SubscriberQueueHeader) <= std::numeric_limits<std::uint16_t>::max(),
              "subscriber queue header size exceeds its wire field");

class PthreadLock {
  public:
    explicit PthreadLock(pthread_mutex_t& mutex) noexcept
        : mutex_(&mutex), locked_(::pthread_mutex_lock(mutex_) == 0) {}

    ~PthreadLock() {
        if (locked_) {
            (void)::pthread_mutex_unlock(mutex_);
        }
    }

    PthreadLock(const PthreadLock&) = delete;
    PthreadLock& operator=(const PthreadLock&) = delete;

    bool locked() const noexcept { return locked_; }
    void unlock() noexcept {
        if (locked_) {
            (void)::pthread_mutex_unlock(mutex_);
            locked_ = false;
        }
    }

  private:
    pthread_mutex_t* mutex_;
    bool locked_;
};

timespec monotonicDeadline(std::uint64_t timeout_ms) noexcept {
    timespec deadline{};
    if (::clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        return deadline;
    }
    const std::uint64_t seconds = timeout_ms / 1000U;
    const std::uint64_t nanoseconds = (timeout_ms % 1000U) * 1000000U;
    const auto maximum_seconds = static_cast<std::uint64_t>(std::numeric_limits<time_t>::max());
    if (seconds > maximum_seconds - static_cast<std::uint64_t>(deadline.tv_sec)) {
        deadline.tv_sec = std::numeric_limits<time_t>::max();
        deadline.tv_nsec = 999999999L;
        return deadline;
    }
    deadline.tv_sec += static_cast<time_t>(seconds);
    deadline.tv_nsec += static_cast<long>(nanoseconds);
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
}

} // namespace

std::size_t SharedSubscriberQueue::requiredSegmentSize(std::uint32_t capacity) {
    if (capacity == 0U || capacity > kMaxSubscriberQueueDepth) {
        throw std::invalid_argument("subscriber queue depth is outside the supported range");
    }
    std::size_t slot_bytes = 0;
    std::size_t total = 0;
    if (!checkedMultiply(capacity, sizeof(ChunkHandle), slot_bytes) ||
        !checkedAdd(slotsOffset(), slot_bytes, total)) {
        throw std::overflow_error("subscriber queue segment size overflow");
    }
    return total;
}

void SharedSubscriberQueue::validateDescriptor(const QueueDescriptor& descriptor) {
    if (!isValidSharedMemoryName(descriptor.shm_name) || descriptor.queue_id == 0U ||
        descriptor.layout_version != kQueueLayoutVersion || descriptor.capacity == 0U ||
        descriptor.capacity > kMaxSubscriberQueueDepth ||
        !validOverflowPolicy(descriptor.overflow_policy) || descriptor.block_timeout_ms == 0U ||
        descriptor.segment_size != requiredSegmentSize(descriptor.capacity)) {
        throw std::invalid_argument("invalid subscriber queue descriptor");
    }
}

std::unique_ptr<SharedSubscriberQueue>
SharedSubscriberQueue::create(const QueueDescriptor& descriptor) {
    validateDescriptor(descriptor);
    SharedMemoryRegion region =
        SharedMemoryRegion::create(descriptor.shm_name, descriptor.segment_size);
    std::memset(region.data(), 0, region.size());
    auto* header = ::new (region.data()) SubscriberQueueHeader{};
    auto* slot_bytes = static_cast<std::uint8_t*>(region.data()) + slotsOffset();
    for (std::uint32_t index = 0; index < descriptor.capacity; ++index) {
        (void)::new (slot_bytes + (index * sizeof(ChunkHandle))) ChunkHandle{};
    }

    pthread_mutexattr_t mutex_attributes{};
    pthread_condattr_t condition_attributes{};
    bool mutex_attributes_initialized = false;
    bool condition_attributes_initialized = false;
    bool mutex_initialized = false;
    bool condition_initialized = false;
    try {
        int error = ::pthread_mutexattr_init(&mutex_attributes);
        if (error != 0) {
            throw std::system_error(error, std::generic_category(), "pthread_mutexattr_init");
        }
        mutex_attributes_initialized = true;
        error = ::pthread_mutexattr_setpshared(&mutex_attributes, PTHREAD_PROCESS_SHARED);
        if (error != 0) {
            throw std::system_error(error, std::generic_category(),
                                    "pthread_mutexattr_setpshared");
        }
        error = ::pthread_mutex_init(&header->mutex, &mutex_attributes);
        if (error != 0) {
            throw std::system_error(error, std::generic_category(), "pthread_mutex_init");
        }
        mutex_initialized = true;

        error = ::pthread_condattr_init(&condition_attributes);
        if (error != 0) {
            throw std::system_error(error, std::generic_category(), "pthread_condattr_init");
        }
        condition_attributes_initialized = true;
        error = ::pthread_condattr_setpshared(&condition_attributes, PTHREAD_PROCESS_SHARED);
        if (error != 0) {
            throw std::system_error(error, std::generic_category(),
                                    "pthread_condattr_setpshared");
        }
        error = ::pthread_condattr_setclock(&condition_attributes, CLOCK_MONOTONIC);
        if (error != 0) {
            throw std::system_error(error, std::generic_category(), "pthread_condattr_setclock");
        }
        error = ::pthread_cond_init(&header->not_full, &condition_attributes);
        if (error != 0) {
            throw std::system_error(error, std::generic_category(), "pthread_cond_init");
        }
        condition_initialized = true;

        header->version = kQueueLayoutVersion;
        header->header_size = static_cast<std::uint16_t>(sizeof(SubscriberQueueHeader));
        header->segment_size = descriptor.segment_size;
        header->queue_id = descriptor.queue_id;
        header->block_timeout_ms = descriptor.block_timeout_ms;
        header->capacity = descriptor.capacity;
        header->attachment_count = 1U;
        header->overflow_policy = static_cast<std::uint16_t>(descriptor.overflow_policy);
        header->magic = kSubscriberQueueMagic;
    } catch (...) {
        if (condition_initialized) {
            (void)::pthread_cond_destroy(&header->not_full);
        }
        if (mutex_initialized) {
            (void)::pthread_mutex_destroy(&header->mutex);
        }
        if (condition_attributes_initialized) {
            (void)::pthread_condattr_destroy(&condition_attributes);
        }
        if (mutex_attributes_initialized) {
            (void)::pthread_mutexattr_destroy(&mutex_attributes);
        }
        throw;
    }
    (void)::pthread_condattr_destroy(&condition_attributes);
    (void)::pthread_mutexattr_destroy(&mutex_attributes);

    return std::unique_ptr<SharedSubscriberQueue>(
        new SharedSubscriberQueue(descriptor, std::move(region), true, true));
}

std::unique_ptr<SharedSubscriberQueue>
SharedSubscriberQueue::open(const QueueDescriptor& descriptor) {
    validateDescriptor(descriptor);
    SharedMemoryRegion region =
        SharedMemoryRegion::openReadWrite(descriptor.shm_name, descriptor.segment_size);
    auto* header = static_cast<SubscriberQueueHeader*>(region.data());
    if (header->magic != kSubscriberQueueMagic || header->version != kQueueLayoutVersion ||
        header->header_size != sizeof(SubscriberQueueHeader) ||
        header->segment_size != descriptor.segment_size || header->queue_id != descriptor.queue_id ||
        header->capacity != descriptor.capacity ||
        header->overflow_policy != static_cast<std::uint16_t>(descriptor.overflow_policy) ||
        header->block_timeout_ms != descriptor.block_timeout_ms || header->head >= header->capacity ||
        header->tail >= header->capacity || header->size > header->capacity) {
        throw MiddlewareError(ErrorCode::InvalidSharedMemory,
                              "subscriber queue metadata does not match its descriptor");
    }

    PthreadLock lock{header->mutex};
    if (!lock.locked()) {
        throw MiddlewareError(ErrorCode::SharedMemoryError,
                              "subscriber queue mutex could not be locked");
    }
    if (header->closed != 0U || header->attachment_count ==
                                    std::numeric_limits<std::uint32_t>::max()) {
        throw MiddlewareError(ErrorCode::QueueClosed, "subscriber queue is closed");
    }
    ++header->attachment_count;
    lock.unlock();
    return std::unique_ptr<SharedSubscriberQueue>(
        new SharedSubscriberQueue(descriptor, std::move(region), false, true));
}

SharedSubscriberQueue::SharedSubscriberQueue(QueueDescriptor descriptor, SharedMemoryRegion region,
                                             bool owner, bool attached) noexcept
    : descriptor_(std::move(descriptor)), region_(std::move(region)),
      header_(static_cast<SubscriberQueueHeader*>(region_.data())), owner_(owner),
      attached_(attached) {}

SharedSubscriberQueue::~SharedSubscriberQueue() {
    if (owner_) {
        close();
    }
    detach();
}

QueueEnqueueResult SharedSubscriberQueue::enqueue(const ChunkHandle& handle,
                                                  QueueNotifyCallback notify,
                                                  void* notify_context) noexcept {
    PthreadLock lock{header_->mutex};
    if (!lock.locked()) {
        return {QueueEnqueueStatus::SynchronizationError, std::nullopt, false};
    }
    if (header_->closed != 0U) {
        return {QueueEnqueueStatus::Closed, std::nullopt, false};
    }

    std::optional<ChunkHandle> dropped;
    bool needs_notification = header_->size == 0U;
    const auto policy = static_cast<OverflowPolicy>(header_->overflow_policy);
    if (header_->size == header_->capacity) {
        if (policy == OverflowPolicy::DropNewest) {
            ++header_->dropped_newest;
            return {QueueEnqueueStatus::DroppedNewest, std::nullopt, false};
        }
        if (policy == OverflowPolicy::DropOldest) {
            dropped = slots()[header_->head];
            header_->head = (header_->head + 1U) % header_->capacity;
            --header_->size;
            ++header_->dropped_oldest;
            needs_notification = false;
        } else {
            const timespec deadline = monotonicDeadline(header_->block_timeout_ms);
            while (header_->size == header_->capacity && header_->closed == 0U) {
                const int error = ::pthread_cond_timedwait(&header_->not_full, &header_->mutex,
                                                           &deadline);
                if (error == ETIMEDOUT) {
                    ++header_->block_timeouts;
                    return {QueueEnqueueStatus::TimedOut, std::nullopt, false};
                }
                if (error != 0) {
                    return {QueueEnqueueStatus::SynchronizationError, std::nullopt, false};
                }
            }
            if (header_->closed != 0U) {
                return {QueueEnqueueStatus::Closed, std::nullopt, false};
            }
            needs_notification = header_->size == 0U;
        }
    }

    slots()[header_->tail] = handle;
    header_->tail = (header_->tail + 1U) % header_->capacity;
    ++header_->size;
    ++header_->enqueued;
    header_->max_size = std::max(header_->max_size, header_->size);

    bool notification_sent = false;
    if (needs_notification && notify != nullptr) {
        notification_sent = notify(notify_context);
        if (!notification_sent) {
            header_->tail = header_->tail == 0U ? header_->capacity - 1U : header_->tail - 1U;
            --header_->size;
            --header_->enqueued;
            return {QueueEnqueueStatus::NotificationFailed, std::nullopt, false};
        }
    }
    return {QueueEnqueueStatus::Accepted, dropped, notification_sent};
}

std::optional<ChunkHandle> SharedSubscriberQueue::tryDequeue() noexcept {
    PthreadLock lock{header_->mutex};
    if (!lock.locked() || header_->size == 0U) {
        return std::nullopt;
    }
    const ChunkHandle handle = slots()[header_->head];
    header_->head = (header_->head + 1U) % header_->capacity;
    --header_->size;
    ++header_->dequeued;
    (void)::pthread_cond_signal(&header_->not_full);
    return handle;
}

std::optional<ChunkHandle> SharedSubscriberQueue::peek() noexcept {
    PthreadLock lock{header_->mutex};
    if (!lock.locked() || header_->size == 0U) {
        return std::nullopt;
    }
    return slots()[header_->head];
}

std::vector<ChunkHandle> SharedSubscriberQueue::drain() {
    PthreadLock lock{header_->mutex};
    if (!lock.locked()) {
        throw MiddlewareError(ErrorCode::SharedMemoryError,
                              "subscriber queue mutex could not be locked");
    }
    std::vector<ChunkHandle> handles;
    handles.reserve(header_->size);
    while (header_->size != 0U) {
        handles.push_back(slots()[header_->head]);
        header_->head = (header_->head + 1U) % header_->capacity;
        --header_->size;
    }
    header_->tail = header_->head;
    (void)::pthread_cond_broadcast(&header_->not_full);
    return handles;
}

std::vector<ChunkHandle> SharedSubscriberQueue::closeAndDrain() {
    PthreadLock lock{header_->mutex};
    if (!lock.locked()) {
        throw MiddlewareError(ErrorCode::SharedMemoryError,
                              "subscriber queue mutex could not be locked");
    }
    header_->closed = 1U;
    std::vector<ChunkHandle> handles;
    handles.reserve(header_->size);
    while (header_->size != 0U) {
        handles.push_back(slots()[header_->head]);
        header_->head = (header_->head + 1U) % header_->capacity;
        --header_->size;
    }
    header_->tail = header_->head;
    (void)::pthread_cond_broadcast(&header_->not_full);
    return handles;
}

QueueStats SharedSubscriberQueue::stats() noexcept {
    PthreadLock lock{header_->mutex};
    if (!lock.locked()) {
        return {};
    }
    return {header_->enqueued,       header_->dequeued,      header_->dropped_newest,
            header_->dropped_oldest, header_->block_timeouts, header_->size,
            header_->max_size};
}

ChunkHandle* SharedSubscriberQueue::slots() noexcept {
    auto* bytes = static_cast<std::uint8_t*>(region_.data());
    return reinterpret_cast<ChunkHandle*>(bytes + slotsOffset());
}

const ChunkHandle* SharedSubscriberQueue::slots() const noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(region_.data());
    return reinterpret_cast<const ChunkHandle*>(bytes + slotsOffset());
}

void SharedSubscriberQueue::close() noexcept {
    if (!attached_) {
        return;
    }
    PthreadLock lock{header_->mutex};
    if (!lock.locked()) {
        return;
    }
    header_->closed = 1U;
    (void)::pthread_cond_broadcast(&header_->not_full);
}

void SharedSubscriberQueue::detach() noexcept {
    if (!attached_) {
        return;
    }
    bool last_attachment = false;
    {
        PthreadLock lock{header_->mutex};
        if (!lock.locked() || header_->attachment_count == 0U) {
            attached_ = false;
            return;
        }
        --header_->attachment_count;
        last_attachment = header_->attachment_count == 0U;
    }
    attached_ = false;
    if (last_attachment) {
        (void)::pthread_cond_destroy(&header_->not_full);
        (void)::pthread_mutex_destroy(&header_->mutex);
    }
}

} // namespace mw::detail
