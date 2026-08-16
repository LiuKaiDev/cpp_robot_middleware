#include "detail/memory_pool.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {

mw::MemoryPoolConfig poolConfig(std::uint32_t count = 1U) {
    mw::MemoryPoolConfig config;
    config.size_classes = {{256U, count},
                           {4U * 1024U, count},
                           {64U * 1024U, count},
                           {1024U * 1024U, count},
                           {4U * 1024U * 1024U, count}};
    return config;
}

mw::detail::PoolDescriptor descriptor(const std::string& label,
                                      const mw::MemoryPoolConfig& config) {
    const std::uint64_t pool_id =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(::getpid())) << 32U) |
        static_cast<std::uint64_t>(label.size() + 1U);
    return {"/mw_pool_unit_" + std::to_string(::getpid()) + "_" + label, pool_id,
            mw::detail::MemoryPool::requiredSegmentSize(config), 71U,
            mw::detail::kPoolLayoutVersion};
}

void publishOne(mw::detail::MemoryPool& pool, const mw::detail::ChunkHandle& handle,
                const std::vector<std::uint8_t>& payload, std::uint64_t sequence,
                std::uint32_t references) {
    ASSERT_EQ(pool.writeAndPublish(handle, payload.data(), payload.size(), sequence, sequence * 10U,
                                   references),
              mw::ErrorCode::Ok);
}

TEST(MemoryPoolTest, InitializesAndSelectsSmallestFittingSizeClass) {
    const auto config = poolConfig();
    const auto info = descriptor("classes", config);
    auto pool = mw::detail::MemoryPool::create(info, config);

    const std::vector<std::pair<std::size_t, std::uint32_t>> cases{
        {0U, 256U},
        {256U, 256U},
        {1000U, 4U * 1024U},
        {4U * 1024U, 4U * 1024U},
        {64U * 1024U, 64U * 1024U},
        {1024U * 1024U, 1024U * 1024U},
        {4U * 1024U * 1024U, 4U * 1024U * 1024U},
    };
    for (const auto& item : cases) {
        const auto allocation = pool->allocate(item.first);
        ASSERT_EQ(allocation.error, mw::ErrorCode::Ok) << item.first;
        EXPECT_EQ(allocation.capacity, item.second);
        EXPECT_EQ(pool->snapshot(allocation.handle.chunk_index).state,
                  mw::detail::ChunkState::Loaned);
        EXPECT_EQ(pool->cancel(allocation.handle), mw::ErrorCode::Ok);
    }

    EXPECT_EQ(pool->allocate(4U * 1024U * 1024U + 1U).error, mw::ErrorCode::MessageTooLarge);
    const auto instrumentation = pool->instrumentation();
    EXPECT_EQ(instrumentation.shm_create_calls, 1U);
    EXPECT_EQ(instrumentation.truncate_calls, 1U);
    EXPECT_EQ(instrumentation.writable_map_calls, 1U);
}

TEST(MemoryPoolTest, ReportsExhaustionAndNeverDuplicatesActiveAllocation) {
    const auto config = poolConfig();
    auto pool = mw::detail::MemoryPool::create(descriptor("exhaust", config), config);
    const auto first = pool->allocate(64U);
    ASSERT_EQ(first.error, mw::ErrorCode::Ok);
    EXPECT_EQ(pool->allocate(64U).error, mw::ErrorCode::PoolExhausted);
    EXPECT_EQ(pool->cancel(first.handle), mw::ErrorCode::Ok);
    const auto second = pool->allocate(64U);
    ASSERT_EQ(second.error, mw::ErrorCode::Ok);
    EXPECT_EQ(second.handle.chunk_index, first.handle.chunk_index);
    EXPECT_NE(second.handle.generation, first.handle.generation);
    EXPECT_EQ(pool->cancel(second.handle), mw::ErrorCode::Ok);
}

TEST(MemoryPoolTest, EnforcesStateReferenceAndGenerationLifecycle) {
    const auto config = poolConfig();
    auto pool = mw::detail::MemoryPool::create(descriptor("lifecycle", config), config);
    const auto allocation = pool->allocate(1000U);
    ASSERT_EQ(allocation.error, mw::ErrorCode::Ok);
    EXPECT_EQ(pool->snapshot(allocation.handle.chunk_index).state, mw::detail::ChunkState::Loaned);

    std::vector<std::uint8_t> payload(1000U, 0x5AU);
    publishOne(*pool, allocation.handle, payload, 8U, 4U);
    auto snapshot = pool->snapshot(allocation.handle.chunk_index);
    EXPECT_EQ(snapshot.state, mw::detail::ChunkState::Published);
    EXPECT_EQ(snapshot.ref_count, 4U);
    EXPECT_EQ(pool->reclaim(allocation.handle), mw::ErrorCode::InvalidState);

    for (const std::uint32_t expected : {3U, 2U, 1U}) {
        const auto release = pool->release(allocation.handle);
        EXPECT_EQ(release.error, mw::ErrorCode::Ok);
        EXPECT_EQ(release.remaining_references, expected);
        EXPECT_FALSE(release.became_released);
    }
    const auto final_release = pool->release(allocation.handle);
    EXPECT_EQ(final_release.error, mw::ErrorCode::Ok);
    EXPECT_EQ(final_release.remaining_references, 0U);
    EXPECT_TRUE(final_release.became_released);
    EXPECT_EQ(pool->snapshot(allocation.handle.chunk_index).state,
              mw::detail::ChunkState::Released);
    EXPECT_EQ(pool->release(allocation.handle).error, mw::ErrorCode::DuplicateRelease);
    EXPECT_EQ(pool->reclaim(allocation.handle), mw::ErrorCode::Ok);
    EXPECT_EQ(pool->snapshot(allocation.handle.chunk_index).state, mw::detail::ChunkState::Free);
    EXPECT_EQ(pool->writeAndPublish(allocation.handle, payload.data(), payload.size(), 9U, 90U, 1U),
              mw::ErrorCode::InvalidState);

    const auto reused = pool->allocate(1000U);
    ASSERT_EQ(reused.error, mw::ErrorCode::Ok);
    EXPECT_EQ(reused.handle.chunk_index, allocation.handle.chunk_index);
    EXPECT_NE(reused.handle.generation, allocation.handle.generation);
    EXPECT_EQ(pool->release(allocation.handle).error, mw::ErrorCode::InvalidChunkHandle);
    EXPECT_EQ(pool->cancel(reused.handle), mw::ErrorCode::Ok);
}

TEST(MemoryPoolTest, PublishesWritableLoanWithoutPayloadCopyAndSupportsGuardReference) {
    mw::MemoryPoolConfig config;
    config.size_classes = {{4096U, 1U}};
    const auto info = descriptor("loan", config);
    auto pool = mw::detail::MemoryPool::create(info, config);
    auto view = mw::detail::MemoryPoolView::open(info, info.topic_id);
    const auto allocation = pool->allocate(1000U);
    ASSERT_EQ(allocation.error, mw::ErrorCode::Ok);
    const auto writable = pool->writablePayload(allocation.handle);
    ASSERT_EQ(writable.error, mw::ErrorCode::Ok);
    ASSERT_NE(writable.payload, nullptr);
    std::memset(writable.payload, 0xA5, 1000U);

    ASSERT_EQ(pool->publishLoaned(allocation.handle, 1000U, 17U, 170U, 1U), mw::ErrorCode::Ok);
    ASSERT_EQ(pool->addReference(allocation.handle), mw::ErrorCode::Ok);
    EXPECT_EQ(pool->snapshot(allocation.handle.chunk_index).ref_count, 2U);
    const auto shared = view->read(allocation.handle, 4096U);
    ASSERT_EQ(shared.error, mw::ErrorCode::Ok);
    EXPECT_EQ(shared.sequence, 17U);
    EXPECT_EQ(shared.payload_size, 1000U);
    EXPECT_TRUE(std::all_of(shared.payload, shared.payload + shared.payload_size,
                            [](std::uint8_t value) { return value == 0xA5U; }));

    EXPECT_FALSE(pool->release(allocation.handle).became_released);
    EXPECT_TRUE(pool->release(allocation.handle).became_released);
    EXPECT_EQ(pool->reclaim(allocation.handle), mw::ErrorCode::Ok);
    EXPECT_EQ(pool->instrumentation().payload_copies, 0U);
}

TEST(MemoryPoolTest, ReusesOneMappedChunkForThousandsOfMessages) {
    mw::MemoryPoolConfig config;
    config.size_classes = {{4U * 1024U, 1U}};
    const auto info = descriptor("reuse", config);
    auto pool = mw::detail::MemoryPool::create(info, config);
    auto view = mw::detail::MemoryPoolView::open(info, info.topic_id);
    std::vector<std::uint8_t> payload(1000U);
    std::uint32_t chunk_index = 0U;

    for (std::uint64_t sequence = 1U; sequence <= 3000U; ++sequence) {
        for (std::size_t index = 0; index < payload.size(); ++index) {
            payload[index] = static_cast<std::uint8_t>((sequence + index) & 0xFFU);
        }
        const auto allocation = pool->allocate(payload.size());
        ASSERT_EQ(allocation.error, mw::ErrorCode::Ok) << sequence;
        if (sequence == 1U) {
            chunk_index = allocation.handle.chunk_index;
        }
        EXPECT_EQ(allocation.handle.chunk_index, chunk_index);
        publishOne(*pool, allocation.handle, payload, sequence, 1U);
        const auto shared = view->read(allocation.handle, payload.size());
        ASSERT_EQ(shared.error, mw::ErrorCode::Ok);
        ASSERT_EQ(shared.payload_size, payload.size());
        EXPECT_EQ(shared.sequence, sequence);
        EXPECT_EQ(std::vector<std::uint8_t>(shared.payload, shared.payload + shared.payload_size),
                  payload);
        const auto release = pool->release(allocation.handle);
        ASSERT_EQ(release.error, mw::ErrorCode::Ok);
        ASSERT_TRUE(release.became_released);
        ASSERT_EQ(pool->reclaim(allocation.handle), mw::ErrorCode::Ok);
    }

    const auto instrumentation = pool->instrumentation();
    EXPECT_EQ(instrumentation.shm_create_calls, 1U);
    EXPECT_EQ(instrumentation.truncate_calls, 1U);
    EXPECT_EQ(instrumentation.writable_map_calls, 1U);
    EXPECT_EQ(instrumentation.allocations, 3000U);
    EXPECT_EQ(instrumentation.payload_copies, 3000U);
    EXPECT_EQ(instrumentation.releases, 3000U);
    EXPECT_EQ(instrumentation.reclaims, 3000U);
    EXPECT_EQ(instrumentation.allocation_failures, 0U);
}

TEST(MemoryPoolTest, RejectsInvalidConfigurationAndUnlinksOwnedPool) {
    mw::MemoryPoolConfig invalid;
    invalid.size_classes = {{4096U, 1U}, {256U, 1U}};
    EXPECT_THROW(mw::detail::MemoryPool::requiredSegmentSize(invalid), std::invalid_argument);

    const auto config = poolConfig();
    const auto info = descriptor("cleanup", config);
    {
        auto pool = mw::detail::MemoryPool::create(info, config);
        const int descriptor_fd = ::shm_open(info.shm_name.c_str(), O_RDONLY | O_CLOEXEC, 0);
        ASSERT_GE(descriptor_fd, 0);
        (void)::close(descriptor_fd);
    }
    errno = 0;
    EXPECT_EQ(::shm_open(info.shm_name.c_str(), O_RDONLY | O_CLOEXEC, 0), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST(MemoryPoolTest, ReadOnlyViewRejectsCorruptHeaderAndDirectoryBounds) {
    const auto config = poolConfig();
    const auto info = descriptor("corrupt", config);
    auto pool = mw::detail::MemoryPool::create(info, config);
    const int descriptor_fd = ::shm_open(info.shm_name.c_str(), O_RDWR | O_CLOEXEC, 0);
    ASSERT_GE(descriptor_fd, 0);
    void* mapping =
        ::mmap(nullptr, info.segment_size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_fd, 0);
    ASSERT_NE(mapping, MAP_FAILED);
    auto* bytes = static_cast<std::uint8_t*>(mapping);

    const std::uint8_t original_magic = bytes[0];
    bytes[0] = 0U;
    EXPECT_THROW(mw::detail::MemoryPoolView::open(info, info.topic_id), mw::MiddlewareError);
    bytes[0] = original_magic;

    constexpr std::size_t directory_offset =
        mw::detail::kPoolHeaderSize + (5U * mw::detail::kSizeClassMetadataSize);
    std::uint8_t original_entry[mw::detail::kChunkDirectoryEntrySize]{};
    std::memcpy(original_entry, bytes + directory_offset, sizeof(original_entry));
    std::memset(bytes + directory_offset, 0xFF, sizeof(original_entry));
    EXPECT_THROW(mw::detail::MemoryPoolView::open(info, info.topic_id), mw::MiddlewareError);
    std::memcpy(bytes + directory_offset, original_entry, sizeof(original_entry));

    EXPECT_EQ(::munmap(mapping, info.segment_size), 0);
    EXPECT_EQ(::close(descriptor_fd), 0);
}

} // namespace
