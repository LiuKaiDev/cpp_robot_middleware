#include "detail/subscriber_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

std::atomic<std::uint32_t> queue_counter{1U};

mw::detail::QueueDescriptor descriptor(const std::string& label, std::uint32_t capacity,
                                       mw::OverflowPolicy policy,
                                       std::chrono::milliseconds timeout = 100ms) {
    const std::uint64_t id =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(::getpid())) << 32U) |
        queue_counter.fetch_add(1U);
    return {"/mw_q5_unit_" + std::to_string(::getpid()) + "_" + label + "_" + std::to_string(id),
            id,
            mw::detail::SharedSubscriberQueue::requiredSegmentSize(capacity),
            capacity,
            mw::detail::kQueueLayoutVersion,
            policy,
            static_cast<std::uint64_t>(timeout.count())};
}

mw::detail::ChunkHandle handle(std::uint32_t value) {
    return {41U, value, value + 1U, 4096U + value};
}

bool countWake(void* context) noexcept {
    ++*static_cast<std::uint32_t*>(context);
    return true;
}

TEST(SubscriberQueueTest, HandlesEmptyFifoWraparoundAndLongRuns) {
    const auto info = descriptor("fifo", 7U, mw::OverflowPolicy::DropNewest);
    auto owner = mw::detail::SharedSubscriberQueue::create(info);
    auto producer = mw::detail::SharedSubscriberQueue::open(info);
    EXPECT_FALSE(owner->tryDequeue().has_value());
    EXPECT_FALSE(owner->peek().has_value());

    std::uint32_t wake_count = 0U;
    for (std::uint32_t cycle = 0U; cycle < 2000U; ++cycle) {
        for (std::uint32_t index = 0U; index < info.capacity; ++index) {
            const auto result =
                producer->enqueue(handle(cycle * info.capacity + index), countWake, &wake_count);
            ASSERT_EQ(result.status, mw::detail::QueueEnqueueStatus::Accepted);
        }
        for (std::uint32_t index = 0U; index < info.capacity; ++index) {
            const auto item = owner->tryDequeue();
            ASSERT_TRUE(item.has_value());
            EXPECT_EQ(*item, handle(cycle * info.capacity + index));
        }
    }
    EXPECT_EQ(wake_count, 2000U);
    const auto stats = owner->stats();
    EXPECT_EQ(stats.enqueued, 14000U);
    EXPECT_EQ(stats.dequeued, 14000U);
    EXPECT_EQ(stats.current_size, 0U);
    EXPECT_EQ(stats.max_size, info.capacity);
}

TEST(SubscriberQueueTest, DropNewestPreservesExistingEntries) {
    const auto info = descriptor("newest", 2U, mw::OverflowPolicy::DropNewest);
    auto queue = mw::detail::SharedSubscriberQueue::create(info);
    EXPECT_EQ(queue->enqueue(handle(1U), nullptr, nullptr).status,
              mw::detail::QueueEnqueueStatus::Accepted);
    EXPECT_EQ(queue->enqueue(handle(2U), nullptr, nullptr).status,
              mw::detail::QueueEnqueueStatus::Accepted);
    EXPECT_EQ(queue->enqueue(handle(3U), nullptr, nullptr).status,
              mw::detail::QueueEnqueueStatus::DroppedNewest);
    EXPECT_EQ(queue->tryDequeue(), handle(1U));
    EXPECT_EQ(queue->tryDequeue(), handle(2U));
    EXPECT_EQ(queue->stats().dropped_newest, 1U);
}

TEST(SubscriberQueueTest, DropOldestReportsDisplacedEntryAndKeepsNewest) {
    const auto info = descriptor("oldest", 2U, mw::OverflowPolicy::DropOldest);
    auto queue = mw::detail::SharedSubscriberQueue::create(info);
    EXPECT_EQ(queue->enqueue(handle(1U), nullptr, nullptr).status,
              mw::detail::QueueEnqueueStatus::Accepted);
    EXPECT_EQ(queue->enqueue(handle(2U), nullptr, nullptr).status,
              mw::detail::QueueEnqueueStatus::Accepted);
    const auto replacement = queue->enqueue(handle(3U), nullptr, nullptr);
    ASSERT_EQ(replacement.status, mw::detail::QueueEnqueueStatus::Accepted);
    ASSERT_TRUE(replacement.dropped_oldest.has_value());
    EXPECT_EQ(*replacement.dropped_oldest, handle(1U));
    EXPECT_EQ(queue->tryDequeue(), handle(2U));
    EXPECT_EQ(queue->tryDequeue(), handle(3U));
    EXPECT_EQ(queue->stats().dropped_oldest, 1U);
}

TEST(SubscriberQueueTest, BlockPolicyTimesOutWithMonotonicDeadline) {
    const auto info = descriptor("timeout", 1U, mw::OverflowPolicy::BlockWithTimeout, 60ms);
    auto queue = mw::detail::SharedSubscriberQueue::create(info);
    ASSERT_EQ(queue->enqueue(handle(1U), nullptr, nullptr).status,
              mw::detail::QueueEnqueueStatus::Accepted);
    const auto start = std::chrono::steady_clock::now();
    const auto result = queue->enqueue(handle(2U), nullptr, nullptr);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_EQ(result.status, mw::detail::QueueEnqueueStatus::TimedOut);
    EXPECT_GE(elapsed, 40ms);
    EXPECT_LT(elapsed, 2s);
    EXPECT_EQ(queue->stats().block_timeouts, 1U);
}

TEST(SubscriberQueueTest, BlockPolicyWakesWhenConsumerCreatesSpace) {
    const auto info = descriptor("wake", 1U, mw::OverflowPolicy::BlockWithTimeout, 2s);
    auto consumer = mw::detail::SharedSubscriberQueue::create(info);
    auto producer = mw::detail::SharedSubscriberQueue::open(info);
    ASSERT_EQ(producer->enqueue(handle(1U), nullptr, nullptr).status,
              mw::detail::QueueEnqueueStatus::Accepted);
    auto blocked = std::async(std::launch::async, [&producer] {
        return producer->enqueue(handle(2U), nullptr, nullptr);
    });
    EXPECT_EQ(blocked.wait_for(40ms), std::future_status::timeout);
    EXPECT_EQ(consumer->tryDequeue(), handle(1U));
    ASSERT_EQ(blocked.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(blocked.get().status, mw::detail::QueueEnqueueStatus::Accepted);
    EXPECT_EQ(consumer->tryDequeue(), handle(2U));
}

TEST(SubscriberQueueTest, RejectsZeroAndExcessiveDepth) {
    EXPECT_THROW(mw::detail::SharedSubscriberQueue::requiredSegmentSize(0U), std::invalid_argument);
    EXPECT_THROW(mw::detail::SharedSubscriberQueue::requiredSegmentSize(
                     mw::detail::kMaxSubscriberQueueDepth + 1U),
                 std::invalid_argument);
}

TEST(SubscriberQueueTest, WakeAndReleaseFramesContainMetadataOnly) {
    const mw::detail::PoolDescriptor pool{"/mw_p5_protocol", 91U, 8U * 1024U, 12U,
                                          mw::detail::kPoolLayoutVersion};
    const auto wake = mw::detail::encodeQueueWake({pool, 77U});
    ASSERT_TRUE(wake.has_value());
    EXPECT_EQ(wake->size(), mw::detail::kQueueWakeSize);
    const auto decoded_wake = mw::detail::decodeQueueWake(wake->data(), wake->size());
    ASSERT_TRUE(decoded_wake.has_value());
    EXPECT_EQ(decoded_wake->pool.shm_name, pool.shm_name);
    EXPECT_EQ(decoded_wake->pool.pool_id, pool.pool_id);
    EXPECT_EQ(decoded_wake->queue_id, 77U);

    const auto original = handle(8U);
    const auto release = mw::detail::encodeQueueRelease(original);
    EXPECT_EQ(release.size(), mw::detail::kQueueReleaseSize);
    EXPECT_EQ(mw::detail::decodeQueueRelease(release.data(), release.size()), original);
}

TEST(SubscriberQueueTest, RepairsRobustMutexAfterOwnerProcessDies) {
    const auto info = descriptor("owner_death", 3U, mw::OverflowPolicy::DropOldest);
    auto queue = mw::detail::SharedSubscriberQueue::create(info);
    ASSERT_EQ(queue->enqueue(handle(1U), nullptr, nullptr).status,
              mw::detail::QueueEnqueueStatus::Accepted);

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        ::_exit(queue->abandonMutexForTesting(true) ? 0 : 1);
    }
    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);

    const auto recovered = queue->stats();
    EXPECT_EQ(recovered.owner_death_recoveries, 1U);
    EXPECT_EQ(recovered.current_size, 0U);
    EXPECT_EQ(queue->enqueue(handle(2U), nullptr, nullptr).status,
              mw::detail::QueueEnqueueStatus::Accepted);
    EXPECT_EQ(queue->tryDequeue(), handle(2U));
}

} // namespace
