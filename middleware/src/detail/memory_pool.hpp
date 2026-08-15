#pragma once

#include <mw/config.hpp>
#include <mw/result.hpp>

#include "detail/pool_protocol.hpp"
#include "detail/shared_memory.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace mw::detail {

inline constexpr std::uint32_t kMemoryPoolMagic = 0x4D57504CU;
inline constexpr std::size_t kPoolHeaderSize = 80U;
inline constexpr std::size_t kSizeClassMetadataSize = 24U;
inline constexpr std::size_t kChunkDirectoryEntrySize = 32U;
inline constexpr std::size_t kChunkAlignment = 64U;

enum class ChunkState : std::uint32_t {
    Free = 0,
    Loaned = 1,
    Published = 2,
    Released = 3,
};

struct PoolHeader {
    std::uint32_t magic{kMemoryPoolMagic};
    std::uint16_t version{kPoolLayoutVersion};
    std::uint16_t header_size{kPoolHeaderSize};
    std::uint64_t segment_size{0};
    std::uint64_t pool_id{0};
    std::uint64_t topic_id{0};
    std::uint64_t owner_pid{0};
    std::uint32_t size_class_count{0};
    std::uint32_t chunk_count{0};
    std::uint64_t size_class_offset{0};
    std::uint64_t chunk_directory_offset{0};
    std::uint64_t chunk_storage_offset{0};
};

struct SizeClassMetadata {
    std::uint64_t capacity{0};
    std::uint32_t first_chunk_index{0};
    std::uint32_t chunk_count{0};
};

struct ChunkDirectoryEntry {
    std::uint64_t header_offset{0};
    std::uint64_t payload_offset{0};
    std::uint64_t capacity{0};
    std::uint32_t size_class_index{0};
};

struct alignas(kChunkAlignment) ChunkHeader {
    std::atomic<std::uint32_t> ref_count{0};
    std::atomic<std::uint32_t> state{static_cast<std::uint32_t>(ChunkState::Free)};
    std::uint32_t payload_size{0};
    std::uint32_t capacity{0};
    std::uint32_t size_class_index{0};
    std::uint32_t generation{0};
    std::uint64_t sequence{0};
    std::uint64_t publish_timestamp_ns{0};
    std::uint64_t topic_id{0};
    std::uint64_t pool_id{0};
    std::uint64_t reserved{0};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "Phase 4 requires lock-free shared uint32 atomics");
static_assert(sizeof(ChunkHeader) == kChunkAlignment, "ChunkHeader must occupy one cache line");
static_assert(alignof(ChunkHeader) == kChunkAlignment, "ChunkHeader alignment changed");

struct PoolInstrumentation {
    std::uint64_t shm_create_calls{0};
    std::uint64_t truncate_calls{0};
    std::uint64_t writable_map_calls{0};
    std::uint64_t allocations{0};
    std::uint64_t payload_copies{0};
    std::uint64_t releases{0};
    std::uint64_t reclaims{0};
    std::uint64_t allocation_failures{0};
};

struct ChunkSnapshot {
    ChunkState state{ChunkState::Free};
    std::uint32_t ref_count{0};
    std::uint32_t payload_size{0};
    std::uint32_t capacity{0};
    std::uint32_t size_class_index{0};
    std::uint32_t generation{0};
    std::uint64_t sequence{0};
};

struct AllocationResult {
    ErrorCode error{ErrorCode::Ok};
    ChunkHandle handle;
    std::uint32_t capacity{0};
};

struct ReleaseResult {
    ErrorCode error{ErrorCode::Ok};
    std::uint32_t remaining_references{0};
    bool became_released{false};
};

class MemoryPool {
  public:
    static std::size_t requiredSegmentSize(const MemoryPoolConfig& config);
    static std::unique_ptr<MemoryPool> create(const PoolDescriptor& descriptor,
                                              const MemoryPoolConfig& config);

    ~MemoryPool();

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    AllocationResult allocate(std::size_t payload_size);
    ErrorCode writeAndPublish(const ChunkHandle& handle, const void* data, std::size_t payload_size,
                              std::uint64_t sequence, std::uint64_t publish_timestamp_ns,
                              std::uint32_t subscriber_references);
    ReleaseResult release(const ChunkHandle& handle);
    ErrorCode reclaim(const ChunkHandle& handle);
    ErrorCode cancel(const ChunkHandle& handle);

    ChunkSnapshot snapshot(std::uint32_t chunk_index) const;
    PoolInstrumentation instrumentation() const;
    const PoolDescriptor& descriptor() const noexcept { return descriptor_; }

  private:
    MemoryPool(PoolDescriptor descriptor, SharedMemoryRegion region,
               std::vector<SizeClassMetadata> size_classes,
               std::vector<ChunkDirectoryEntry> directory);

    ChunkHeader* chunkHeader(std::uint32_t chunk_index) noexcept;
    const ChunkHeader* chunkHeader(std::uint32_t chunk_index) const noexcept;
    bool handleMatches(const ChunkHandle& handle, const ChunkHeader& header) const noexcept;

    PoolDescriptor descriptor_;
    SharedMemoryRegion region_;
    std::vector<SizeClassMetadata> size_classes_;
    std::vector<ChunkDirectoryEntry> directory_;
    std::vector<std::vector<std::uint32_t>> free_lists_;
    mutable std::mutex mutex_;
    PoolInstrumentation instrumentation_;
};

struct ChunkViewResult {
    ErrorCode error{ErrorCode::Ok};
    const std::uint8_t* payload{nullptr};
    std::size_t payload_size{0};
    std::uint64_t sequence{0};
    std::uint64_t publish_timestamp_ns{0};
};

class MemoryPoolView {
  public:
    static std::unique_ptr<MemoryPoolView> open(const PoolDescriptor& descriptor,
                                                std::uint64_t expected_topic_id);

    MemoryPoolView(const MemoryPoolView&) = delete;
    MemoryPoolView& operator=(const MemoryPoolView&) = delete;

    ChunkViewResult read(const ChunkHandle& handle, std::size_t max_message_size) const noexcept;
    const PoolDescriptor& descriptor() const noexcept { return descriptor_; }

  private:
    MemoryPoolView(PoolDescriptor descriptor, SharedMemoryRegion region,
                   std::vector<SizeClassMetadata> size_classes,
                   std::vector<ChunkDirectoryEntry> directory) noexcept;

    PoolDescriptor descriptor_;
    SharedMemoryRegion region_;
    std::vector<SizeClassMetadata> size_classes_;
    std::vector<ChunkDirectoryEntry> directory_;
};

} // namespace mw::detail
