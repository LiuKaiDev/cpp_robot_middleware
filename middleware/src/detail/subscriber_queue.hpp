#pragma once

#include <mw/result.hpp>

#include "detail/queue_protocol.hpp"
#include "detail/shared_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <pthread.h>
#include <vector>

namespace mw::detail {

inline constexpr std::uint32_t kSubscriberQueueMagic = 0x4D575151U;
inline constexpr std::uint32_t kMaxSubscriberQueueDepth = 65536U;

struct QueueStats {
    std::uint64_t enqueued{0};
    std::uint64_t dequeued{0};
    std::uint64_t dropped_newest{0};
    std::uint64_t dropped_oldest{0};
    std::uint64_t block_timeouts{0};
    std::uint64_t blocked_count{0};
    std::uint64_t blocked_time_ns{0};
    std::uint64_t owner_death_recoveries{0};
    std::uint64_t peer_resets{0};
    std::uint32_t current_size{0};
    std::uint32_t max_size{0};
};

struct alignas(64) SubscriberQueueHeader {
    std::uint32_t magic{0};
    std::uint16_t version{0};
    std::uint16_t header_size{0};
    std::uint64_t segment_size{0};
    std::uint64_t queue_id{0};
    std::uint64_t block_timeout_ms{0};
    std::uint32_t capacity{0};
    std::uint32_t head{0};
    std::uint32_t tail{0};
    std::uint32_t size{0};
    std::uint32_t max_size{0};
    std::uint32_t attachment_count{0};
    std::uint32_t closed{0};
    std::uint16_t overflow_policy{0};
    std::uint16_t reserved16{0};
    std::uint32_t reserved32{0};
    std::uint64_t enqueued{0};
    std::uint64_t dequeued{0};
    std::uint64_t dropped_newest{0};
    std::uint64_t dropped_oldest{0};
    std::uint64_t block_timeouts{0};
    std::uint64_t blocked_count{0};
    std::uint64_t blocked_time_ns{0};
    std::uint64_t owner_death_recoveries{0};
    std::uint64_t peer_resets{0};
    pthread_mutex_t mutex{};
    pthread_cond_t not_full{};
};

enum class QueueEnqueueStatus {
    Accepted,
    DroppedNewest,
    TimedOut,
    Closed,
    NotificationFailed,
    SynchronizationError,
};

struct QueueEnqueueResult {
    QueueEnqueueStatus status{QueueEnqueueStatus::SynchronizationError};
    std::optional<ChunkHandle> dropped_oldest;
    bool notification_sent{false};
    bool blocked{false};
    std::uint64_t blocked_time_ns{0};
};

using QueueNotifyCallback = bool (*)(void* context) noexcept;

class SharedSubscriberQueue {
  public:
    static std::size_t requiredSegmentSize(std::uint32_t capacity);
    static std::unique_ptr<SharedSubscriberQueue> create(const QueueDescriptor& descriptor);
    static std::unique_ptr<SharedSubscriberQueue> open(const QueueDescriptor& descriptor);

    ~SharedSubscriberQueue();

    SharedSubscriberQueue(const SharedSubscriberQueue&) = delete;
    SharedSubscriberQueue& operator=(const SharedSubscriberQueue&) = delete;

    QueueEnqueueResult enqueue(const ChunkHandle& handle, QueueNotifyCallback notify,
                               void* notify_context) noexcept;
    std::optional<ChunkHandle> tryDequeue() noexcept;
    std::optional<ChunkHandle> peek() noexcept;
    std::vector<ChunkHandle> drain();
    std::vector<ChunkHandle> closeAndDrain();
    void discardPool(std::uint64_t pool_id) noexcept;
    QueueStats stats() noexcept;

    static ErrorCode recoverAndClose(const QueueDescriptor& descriptor) noexcept;
    bool abandonMutexForTesting(bool corrupt_invariants) noexcept;

    const QueueDescriptor& descriptor() const noexcept { return descriptor_; }

  private:
    SharedSubscriberQueue(QueueDescriptor descriptor, SharedMemoryRegion region, bool owner,
                          bool attached) noexcept;

    static void validateDescriptor(const QueueDescriptor& descriptor);
    ChunkHandle* slots() noexcept;
    const ChunkHandle* slots() const noexcept;
    void close() noexcept;
    void detach() noexcept;

    QueueDescriptor descriptor_;
    SharedMemoryRegion region_;
    SubscriberQueueHeader* header_{nullptr};
    bool owner_{false};
    bool attached_{false};
};

} // namespace mw::detail
