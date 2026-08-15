#include "detail/memory_pool.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <unistd.h>
#include <utility>

namespace mw::detail {
namespace {

struct PoolLayout {
    PoolHeader header;
    std::vector<SizeClassMetadata> size_classes;
    std::vector<ChunkDirectoryEntry> directory;
};

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

bool checkedAlign(std::size_t value, std::size_t alignment, std::size_t& result) noexcept {
    const std::size_t remainder = value % alignment;
    if (remainder == 0U) {
        result = value;
        return true;
    }
    return checkedAdd(value, alignment - remainder, result);
}

template <typename Integer> void encodeBigEndian(Integer value, std::uint8_t* output) noexcept {
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        const auto shift = static_cast<unsigned>((sizeof(Integer) - index - 1U) * 8U);
        output[index] = static_cast<std::uint8_t>(value >> shift);
    }
}

template <typename Integer> Integer decodeBigEndian(const std::uint8_t* input) noexcept {
    Integer value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        value = static_cast<Integer>((value << 8U) | input[index]);
    }
    return value;
}

void encodePoolHeader(const PoolHeader& header, std::uint8_t* output) noexcept {
    std::memset(output, 0, kPoolHeaderSize);
    encodeBigEndian(header.magic, output);
    encodeBigEndian(header.version, output + 4U);
    encodeBigEndian(header.header_size, output + 6U);
    encodeBigEndian(header.segment_size, output + 8U);
    encodeBigEndian(header.pool_id, output + 16U);
    encodeBigEndian(header.topic_id, output + 24U);
    encodeBigEndian(header.owner_pid, output + 32U);
    encodeBigEndian(header.size_class_count, output + 40U);
    encodeBigEndian(header.chunk_count, output + 44U);
    encodeBigEndian(header.size_class_offset, output + 48U);
    encodeBigEndian(header.chunk_directory_offset, output + 56U);
    encodeBigEndian(header.chunk_storage_offset, output + 64U);
}

PoolHeader decodePoolHeader(const std::uint8_t* input) noexcept {
    return {
        decodeBigEndian<std::uint32_t>(input),       decodeBigEndian<std::uint16_t>(input + 4U),
        decodeBigEndian<std::uint16_t>(input + 6U),  decodeBigEndian<std::uint64_t>(input + 8U),
        decodeBigEndian<std::uint64_t>(input + 16U), decodeBigEndian<std::uint64_t>(input + 24U),
        decodeBigEndian<std::uint64_t>(input + 32U), decodeBigEndian<std::uint32_t>(input + 40U),
        decodeBigEndian<std::uint32_t>(input + 44U), decodeBigEndian<std::uint64_t>(input + 48U),
        decodeBigEndian<std::uint64_t>(input + 56U), decodeBigEndian<std::uint64_t>(input + 64U)};
}

void encodeSizeClass(const SizeClassMetadata& metadata, std::uint8_t* output) noexcept {
    std::memset(output, 0, kSizeClassMetadataSize);
    encodeBigEndian(metadata.capacity, output);
    encodeBigEndian(metadata.first_chunk_index, output + 8U);
    encodeBigEndian(metadata.chunk_count, output + 12U);
}

SizeClassMetadata decodeSizeClass(const std::uint8_t* input) noexcept {
    return {decodeBigEndian<std::uint64_t>(input), decodeBigEndian<std::uint32_t>(input + 8U),
            decodeBigEndian<std::uint32_t>(input + 12U)};
}

void encodeDirectoryEntry(const ChunkDirectoryEntry& entry, std::uint8_t* output) noexcept {
    std::memset(output, 0, kChunkDirectoryEntrySize);
    encodeBigEndian(entry.header_offset, output);
    encodeBigEndian(entry.payload_offset, output + 8U);
    encodeBigEndian(entry.capacity, output + 16U);
    encodeBigEndian(entry.size_class_index, output + 24U);
}

ChunkDirectoryEntry decodeDirectoryEntry(const std::uint8_t* input) noexcept {
    return {decodeBigEndian<std::uint64_t>(input), decodeBigEndian<std::uint64_t>(input + 8U),
            decodeBigEndian<std::uint64_t>(input + 16U),
            decodeBigEndian<std::uint32_t>(input + 24U)};
}

PoolLayout makeLayout(const MemoryPoolConfig& config, std::uint64_t pool_id,
                      std::uint64_t topic_id) {
    if (config.size_classes.empty() ||
        config.size_classes.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("memory pool must contain size classes");
    }

    PoolLayout layout;
    layout.size_classes.reserve(config.size_classes.size());
    std::uint64_t previous_capacity = 0;
    std::uint64_t total_chunks = 0;
    for (const MemoryPoolClassConfig& item : config.size_classes) {
        if (item.chunk_size == 0U || item.chunk_size > std::numeric_limits<std::uint32_t>::max() ||
            item.chunk_count == 0U || item.chunk_size <= previous_capacity ||
            total_chunks > std::numeric_limits<std::uint32_t>::max() - item.chunk_count) {
            throw std::invalid_argument("invalid memory pool size class configuration");
        }
        layout.size_classes.push_back(
            {item.chunk_size, static_cast<std::uint32_t>(total_chunks), item.chunk_count});
        previous_capacity = item.chunk_size;
        total_chunks += item.chunk_count;
    }

    std::size_t class_bytes = 0;
    std::size_t directory_bytes = 0;
    std::size_t directory_offset = 0;
    std::size_t storage_offset = 0;
    if (!checkedMultiply(layout.size_classes.size(), kSizeClassMetadataSize, class_bytes) ||
        !checkedMultiply(static_cast<std::size_t>(total_chunks), kChunkDirectoryEntrySize,
                         directory_bytes) ||
        !checkedAdd(kPoolHeaderSize, class_bytes, directory_offset) ||
        !checkedAdd(directory_offset, directory_bytes, storage_offset) ||
        !checkedAlign(storage_offset, kChunkAlignment, storage_offset)) {
        throw std::overflow_error("memory pool metadata size overflow");
    }

    layout.directory.reserve(static_cast<std::size_t>(total_chunks));
    std::size_t next_offset = storage_offset;
    for (std::size_t class_index = 0; class_index < layout.size_classes.size(); ++class_index) {
        const SizeClassMetadata& metadata = layout.size_classes[class_index];
        for (std::uint32_t count = 0; count < metadata.chunk_count; ++count) {
            std::size_t header_offset = 0;
            std::size_t payload_offset = 0;
            std::size_t chunk_end = 0;
            if (!checkedAlign(next_offset, kChunkAlignment, header_offset) ||
                !checkedAdd(header_offset, sizeof(ChunkHeader), payload_offset) ||
                !checkedAdd(payload_offset, static_cast<std::size_t>(metadata.capacity),
                            chunk_end) ||
                !checkedAlign(chunk_end, kChunkAlignment, next_offset)) {
                throw std::overflow_error("memory pool chunk layout overflow");
            }
            layout.directory.push_back({header_offset, payload_offset, metadata.capacity,
                                        static_cast<std::uint32_t>(class_index)});
        }
    }

    layout.header = {kMemoryPoolMagic,
                     kPoolLayoutVersion,
                     static_cast<std::uint16_t>(kPoolHeaderSize),
                     next_offset,
                     pool_id,
                     topic_id,
                     static_cast<std::uint64_t>(::getpid()),
                     static_cast<std::uint32_t>(layout.size_classes.size()),
                     static_cast<std::uint32_t>(layout.directory.size()),
                     kPoolHeaderSize,
                     directory_offset,
                     storage_offset};
    return layout;
}

bool rangeInside(std::size_t offset, std::size_t size, std::size_t limit) noexcept {
    return offset <= limit && size <= limit - offset;
}

} // namespace

std::size_t MemoryPool::requiredSegmentSize(const MemoryPoolConfig& config) {
    return static_cast<std::size_t>(makeLayout(config, 1U, 1U).header.segment_size);
}

std::unique_ptr<MemoryPool> MemoryPool::create(const PoolDescriptor& descriptor,
                                               const MemoryPoolConfig& config) {
    if (descriptor.pool_id == 0U || descriptor.topic_id == 0U ||
        descriptor.layout_version != kPoolLayoutVersion) {
        throw std::invalid_argument("invalid memory pool descriptor");
    }
    PoolLayout layout = makeLayout(config, descriptor.pool_id, descriptor.topic_id);
    if (descriptor.segment_size != layout.header.segment_size) {
        throw std::invalid_argument("memory pool descriptor size does not match configuration");
    }

    SharedMemoryRegion region = SharedMemoryRegion::create(
        descriptor.shm_name, static_cast<std::size_t>(layout.header.segment_size));
    auto* bytes = static_cast<std::uint8_t*>(region.data());
    encodePoolHeader(layout.header, bytes);
    for (std::size_t index = 0; index < layout.size_classes.size(); ++index) {
        encodeSizeClass(layout.size_classes[index],
                        bytes + kPoolHeaderSize + (index * kSizeClassMetadataSize));
    }
    for (std::size_t index = 0; index < layout.directory.size(); ++index) {
        encodeDirectoryEntry(layout.directory[index],
                             bytes +
                                 static_cast<std::size_t>(layout.header.chunk_directory_offset) +
                                 (index * kChunkDirectoryEntrySize));
        auto* header = ::new (bytes + layout.directory[index].header_offset) ChunkHeader{};
        header->capacity = static_cast<std::uint32_t>(layout.directory[index].capacity);
        header->size_class_index = layout.directory[index].size_class_index;
        header->topic_id = descriptor.topic_id;
        header->pool_id = descriptor.pool_id;
    }

    return std::unique_ptr<MemoryPool>(new MemoryPool(descriptor, std::move(region),
                                                      std::move(layout.size_classes),
                                                      std::move(layout.directory)));
}

MemoryPool::MemoryPool(PoolDescriptor descriptor, SharedMemoryRegion region,
                       std::vector<SizeClassMetadata> size_classes,
                       std::vector<ChunkDirectoryEntry> directory)
    : descriptor_(std::move(descriptor)), region_(std::move(region)),
      size_classes_(std::move(size_classes)), directory_(std::move(directory)),
      free_lists_(size_classes_.size()) {
    for (std::size_t index = directory_.size(); index > 0U; --index) {
        const std::uint32_t chunk_index = static_cast<std::uint32_t>(index - 1U);
        free_lists_[directory_[chunk_index].size_class_index].push_back(chunk_index);
    }
    instrumentation_.shm_create_calls = 1U;
    instrumentation_.truncate_calls = 1U;
    instrumentation_.writable_map_calls = 1U;
}

MemoryPool::~MemoryPool() {
    for (std::uint32_t index = 0; index < directory_.size(); ++index) {
        chunkHeader(index)->~ChunkHeader();
    }
}

AllocationResult MemoryPool::allocate(std::size_t payload_size) {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto size_class = std::find_if(
        size_classes_.begin(), size_classes_.end(),
        [payload_size](const SizeClassMetadata& item) { return item.capacity >= payload_size; });
    if (size_class == size_classes_.end()) {
        return {ErrorCode::MessageTooLarge, {}, 0};
    }
    const std::size_t class_index =
        static_cast<std::size_t>(std::distance(size_classes_.begin(), size_class));
    if (free_lists_[class_index].empty()) {
        ++instrumentation_.allocation_failures;
        return {ErrorCode::PoolExhausted, {}, 0};
    }

    const std::uint32_t chunk_index = free_lists_[class_index].back();
    free_lists_[class_index].pop_back();
    ChunkHeader* header = chunkHeader(chunk_index);
    if (header->state.load(std::memory_order_acquire) !=
            static_cast<std::uint32_t>(ChunkState::Free) ||
        header->ref_count.load(std::memory_order_relaxed) != 0U) {
        free_lists_[class_index].push_back(chunk_index);
        return {ErrorCode::InvalidState, {}, 0};
    }
    ++header->generation;
    if (header->generation == 0U) {
        ++header->generation;
    }
    header->payload_size = static_cast<std::uint32_t>(payload_size);
    header->sequence = 0U;
    header->publish_timestamp_ns = 0U;
    header->state.store(static_cast<std::uint32_t>(ChunkState::Loaned), std::memory_order_release);
    ++instrumentation_.allocations;
    return {ErrorCode::Ok,
            {descriptor_.pool_id, chunk_index, header->generation,
             directory_[chunk_index].payload_offset},
            header->capacity};
}

WritableChunkResult MemoryPool::writablePayload(const ChunkHandle& handle) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    if (handle.chunk_index >= directory_.size()) {
        return {ErrorCode::InvalidChunkHandle, nullptr};
    }
    ChunkHeader* header = chunkHeader(handle.chunk_index);
    if (!handleMatches(handle, *header)) {
        return {ErrorCode::InvalidChunkHandle, nullptr};
    }
    if (header->state.load(std::memory_order_acquire) !=
        static_cast<std::uint32_t>(ChunkState::Loaned)) {
        return {ErrorCode::InvalidState, nullptr};
    }
    auto* bytes = static_cast<std::uint8_t*>(region_.data());
    return {ErrorCode::Ok, bytes + handle.payload_offset};
}

ErrorCode MemoryPool::writeAndPublish(const ChunkHandle& handle, const void* data,
                                      std::size_t payload_size, std::uint64_t sequence,
                                      std::uint64_t publish_timestamp_ns,
                                      std::uint32_t subscriber_references) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (payload_size != 0U && data == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    const ErrorCode validation = publishLocked(handle, payload_size, sequence,
                                               publish_timestamp_ns, subscriber_references);
    if (validation != ErrorCode::Ok) {
        return validation;
    }
    if (payload_size != 0U) {
        auto* destination = static_cast<std::uint8_t*>(region_.data()) + handle.payload_offset;
        std::memcpy(destination, data, payload_size);
    }
    ChunkHeader* header = chunkHeader(handle.chunk_index);
    header->sequence = sequence;
    header->publish_timestamp_ns = publish_timestamp_ns;
    header->ref_count.store(subscriber_references, std::memory_order_relaxed);
    header->state.store(static_cast<std::uint32_t>(ChunkState::Published),
                        std::memory_order_release);
    ++instrumentation_.payload_copies;
    return ErrorCode::Ok;
}

ErrorCode MemoryPool::publishLoaned(const ChunkHandle& handle, std::size_t payload_size,
                                    std::uint64_t sequence, std::uint64_t publish_timestamp_ns,
                                    std::uint32_t initial_references) {
    std::lock_guard<std::mutex> lock{mutex_};
    const ErrorCode validation = publishLocked(handle, payload_size, sequence,
                                               publish_timestamp_ns, initial_references);
    if (validation != ErrorCode::Ok) {
        return validation;
    }
    ChunkHeader* header = chunkHeader(handle.chunk_index);
    header->sequence = sequence;
    header->publish_timestamp_ns = publish_timestamp_ns;
    header->ref_count.store(initial_references, std::memory_order_relaxed);
    header->state.store(static_cast<std::uint32_t>(ChunkState::Published),
                        std::memory_order_release);
    return ErrorCode::Ok;
}

ErrorCode MemoryPool::addReference(const ChunkHandle& handle) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (handle.chunk_index >= directory_.size()) {
        return ErrorCode::InvalidChunkHandle;
    }
    ChunkHeader* header = chunkHeader(handle.chunk_index);
    if (!handleMatches(handle, *header)) {
        return ErrorCode::InvalidChunkHandle;
    }
    if (header->state.load(std::memory_order_acquire) !=
        static_cast<std::uint32_t>(ChunkState::Published)) {
        return ErrorCode::InvalidState;
    }
    const std::uint32_t references = header->ref_count.load(std::memory_order_relaxed);
    if (references == 0U || references == std::numeric_limits<std::uint32_t>::max()) {
        return ErrorCode::InvalidState;
    }
    header->ref_count.store(references + 1U, std::memory_order_relaxed);
    return ErrorCode::Ok;
}

ReleaseResult MemoryPool::release(const ChunkHandle& handle) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (handle.chunk_index >= directory_.size()) {
        return {ErrorCode::InvalidChunkHandle, 0, false};
    }
    ChunkHeader* header = chunkHeader(handle.chunk_index);
    if (!handleMatches(handle, *header)) {
        return {ErrorCode::InvalidChunkHandle, 0, false};
    }
    if (header->state.load(std::memory_order_acquire) !=
        static_cast<std::uint32_t>(ChunkState::Published)) {
        return {ErrorCode::DuplicateRelease, header->ref_count.load(std::memory_order_relaxed),
                false};
    }
    const std::uint32_t references = header->ref_count.load(std::memory_order_relaxed);
    if (references == 0U) {
        return {ErrorCode::DuplicateRelease, 0, false};
    }
    const std::uint32_t remaining = references - 1U;
    header->ref_count.store(remaining, std::memory_order_relaxed);
    const bool released = remaining == 0U;
    if (released) {
        header->state.store(static_cast<std::uint32_t>(ChunkState::Released),
                            std::memory_order_release);
    }
    ++instrumentation_.releases;
    return {ErrorCode::Ok, remaining, released};
}

ErrorCode MemoryPool::reclaim(const ChunkHandle& handle) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (handle.chunk_index >= directory_.size()) {
        return ErrorCode::InvalidChunkHandle;
    }
    ChunkHeader* header = chunkHeader(handle.chunk_index);
    if (!handleMatches(handle, *header)) {
        return ErrorCode::InvalidChunkHandle;
    }
    if (header->state.load(std::memory_order_acquire) !=
            static_cast<std::uint32_t>(ChunkState::Released) ||
        header->ref_count.load(std::memory_order_relaxed) != 0U) {
        return ErrorCode::InvalidState;
    }
    header->payload_size = 0U;
    header->sequence = 0U;
    header->publish_timestamp_ns = 0U;
    header->state.store(static_cast<std::uint32_t>(ChunkState::Free), std::memory_order_release);
    free_lists_[header->size_class_index].push_back(handle.chunk_index);
    ++instrumentation_.reclaims;
    return ErrorCode::Ok;
}

ErrorCode MemoryPool::cancel(const ChunkHandle& handle) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (handle.chunk_index >= directory_.size()) {
        return ErrorCode::InvalidChunkHandle;
    }
    ChunkHeader* header = chunkHeader(handle.chunk_index);
    if (!handleMatches(handle, *header)) {
        return ErrorCode::InvalidChunkHandle;
    }
    if (header->state.load(std::memory_order_acquire) !=
        static_cast<std::uint32_t>(ChunkState::Loaned)) {
        return ErrorCode::InvalidState;
    }
    header->payload_size = 0U;
    header->state.store(static_cast<std::uint32_t>(ChunkState::Free), std::memory_order_release);
    free_lists_[header->size_class_index].push_back(handle.chunk_index);
    ++instrumentation_.reclaims;
    return ErrorCode::Ok;
}

ChunkSnapshot MemoryPool::snapshot(std::uint32_t chunk_index) const {
    std::lock_guard<std::mutex> lock{mutex_};
    if (chunk_index >= directory_.size()) {
        throw std::out_of_range("chunk index is outside the memory pool");
    }
    const ChunkHeader* header = chunkHeader(chunk_index);
    return {static_cast<ChunkState>(header->state.load(std::memory_order_acquire)),
            header->ref_count.load(std::memory_order_relaxed),
            header->payload_size,
            header->capacity,
            header->size_class_index,
            header->generation,
            header->sequence};
}

PoolInstrumentation MemoryPool::instrumentation() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return instrumentation_;
}

ChunkHeader* MemoryPool::chunkHeader(std::uint32_t chunk_index) noexcept {
    auto* bytes = static_cast<std::uint8_t*>(region_.data());
    return reinterpret_cast<ChunkHeader*>(bytes + directory_[chunk_index].header_offset);
}

const ChunkHeader* MemoryPool::chunkHeader(std::uint32_t chunk_index) const noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(region_.data());
    return reinterpret_cast<const ChunkHeader*>(bytes + directory_[chunk_index].header_offset);
}

bool MemoryPool::handleMatches(const ChunkHandle& handle,
                               const ChunkHeader& header) const noexcept {
    return handle.pool_id == descriptor_.pool_id && handle.generation == header.generation &&
           handle.payload_offset == directory_[handle.chunk_index].payload_offset;
}

ErrorCode MemoryPool::publishLocked(const ChunkHandle& handle, std::size_t payload_size,
                                    std::uint64_t sequence, std::uint64_t,
                                    std::uint32_t initial_references) noexcept {
    if (handle.chunk_index >= directory_.size()) {
        return ErrorCode::InvalidChunkHandle;
    }
    ChunkHeader* header = chunkHeader(handle.chunk_index);
    if (!handleMatches(handle, *header)) {
        return ErrorCode::InvalidChunkHandle;
    }
    if (header->state.load(std::memory_order_acquire) !=
        static_cast<std::uint32_t>(ChunkState::Loaned)) {
        return ErrorCode::InvalidState;
    }
    if (payload_size != header->payload_size || payload_size > header->capacity || sequence == 0U ||
        initial_references == 0U) {
        return ErrorCode::InvalidArgument;
    }
    return ErrorCode::Ok;
}

std::unique_ptr<MemoryPoolView> MemoryPoolView::open(const PoolDescriptor& descriptor,
                                                     std::uint64_t expected_topic_id) {
    if (descriptor.pool_id == 0U || descriptor.segment_size < kPoolHeaderSize ||
        descriptor.segment_size > std::numeric_limits<std::size_t>::max() ||
        descriptor.topic_id != expected_topic_id ||
        descriptor.layout_version != kPoolLayoutVersion) {
        throw MiddlewareError(ErrorCode::InvalidSharedMemory, "invalid pool descriptor");
    }
    SharedMemoryRegion region = SharedMemoryRegion::openReadOnly(
        descriptor.shm_name, static_cast<std::size_t>(descriptor.segment_size));
    const auto* bytes = static_cast<const std::uint8_t*>(region.data());
    const PoolHeader header = decodePoolHeader(bytes);
    if (header.magic != kMemoryPoolMagic || header.version != kPoolLayoutVersion ||
        header.header_size != kPoolHeaderSize || header.segment_size != region.size() ||
        header.pool_id != descriptor.pool_id || header.topic_id != expected_topic_id ||
        header.size_class_count == 0U || header.chunk_count == 0U ||
        header.size_class_offset != kPoolHeaderSize) {
        throw MiddlewareError(ErrorCode::InvalidSharedMemory, "invalid memory pool header");
    }

    std::size_t class_bytes = 0;
    std::size_t directory_bytes = 0;
    if (header.size_class_offset > std::numeric_limits<std::size_t>::max() ||
        header.chunk_directory_offset > std::numeric_limits<std::size_t>::max() ||
        header.chunk_storage_offset > std::numeric_limits<std::size_t>::max() ||
        !checkedMultiply(header.size_class_count, kSizeClassMetadataSize, class_bytes) ||
        !checkedMultiply(header.chunk_count, kChunkDirectoryEntrySize, directory_bytes) ||
        !rangeInside(static_cast<std::size_t>(header.size_class_offset), class_bytes,
                     region.size()) ||
        !rangeInside(static_cast<std::size_t>(header.chunk_directory_offset), directory_bytes,
                     region.size()) ||
        header.chunk_storage_offset > region.size() ||
        header.chunk_storage_offset % kChunkAlignment != 0U) {
        throw MiddlewareError(ErrorCode::InvalidSharedMemory,
                              "memory pool metadata is out of bounds");
    }

    std::vector<SizeClassMetadata> size_classes;
    size_classes.reserve(header.size_class_count);
    std::uint64_t previous_capacity = 0U;
    std::uint64_t expected_first_index = 0U;
    for (std::uint32_t index = 0; index < header.size_class_count; ++index) {
        const SizeClassMetadata metadata =
            decodeSizeClass(bytes + header.size_class_offset + (index * kSizeClassMetadataSize));
        if (metadata.capacity == 0U || metadata.capacity <= previous_capacity ||
            metadata.first_chunk_index != expected_first_index || metadata.chunk_count == 0U ||
            metadata.chunk_count > header.chunk_count ||
            expected_first_index > header.chunk_count - metadata.chunk_count) {
            throw MiddlewareError(ErrorCode::InvalidSharedMemory,
                                  "invalid memory pool size class metadata");
        }
        expected_first_index += metadata.chunk_count;
        previous_capacity = metadata.capacity;
        size_classes.push_back(metadata);
    }
    if (expected_first_index != header.chunk_count) {
        throw MiddlewareError(ErrorCode::InvalidSharedMemory, "memory pool chunk count mismatch");
    }

    std::vector<ChunkDirectoryEntry> directory;
    directory.reserve(header.chunk_count);
    std::size_t previous_end = static_cast<std::size_t>(header.chunk_storage_offset);
    for (std::uint32_t index = 0; index < header.chunk_count; ++index) {
        const ChunkDirectoryEntry entry = decodeDirectoryEntry(
            bytes + header.chunk_directory_offset + (index * kChunkDirectoryEntrySize));
        if (entry.size_class_index >= size_classes.size() ||
            entry.capacity != size_classes[entry.size_class_index].capacity ||
            entry.header_offset > std::numeric_limits<std::uint64_t>::max() - sizeof(ChunkHeader) ||
            entry.payload_offset > std::numeric_limits<std::uint64_t>::max() - entry.capacity ||
            entry.header_offset % kChunkAlignment != 0U ||
            entry.payload_offset != entry.header_offset + sizeof(ChunkHeader) ||
            entry.payload_offset % kChunkAlignment != 0U || entry.header_offset < previous_end ||
            entry.header_offset > std::numeric_limits<std::size_t>::max() ||
            entry.payload_offset > std::numeric_limits<std::size_t>::max() ||
            entry.capacity > std::numeric_limits<std::size_t>::max() ||
            !rangeInside(static_cast<std::size_t>(entry.payload_offset),
                         static_cast<std::size_t>(entry.capacity), region.size())) {
            throw MiddlewareError(ErrorCode::InvalidSharedMemory,
                                  "invalid memory pool chunk directory");
        }
        previous_end = static_cast<std::size_t>(entry.payload_offset) +
                       static_cast<std::size_t>(entry.capacity);
        const auto* chunk = reinterpret_cast<const ChunkHeader*>(bytes + entry.header_offset);
        if (chunk->capacity != entry.capacity ||
            chunk->size_class_index != entry.size_class_index ||
            chunk->topic_id != descriptor.topic_id || chunk->pool_id != descriptor.pool_id) {
            throw MiddlewareError(ErrorCode::InvalidSharedMemory,
                                  "chunk header does not match pool metadata");
        }
        directory.push_back(entry);
    }

    return std::unique_ptr<MemoryPoolView>(new MemoryPoolView(
        descriptor, std::move(region), std::move(size_classes), std::move(directory)));
}

MemoryPoolView::MemoryPoolView(PoolDescriptor descriptor, SharedMemoryRegion region,
                               std::vector<SizeClassMetadata> size_classes,
                               std::vector<ChunkDirectoryEntry> directory) noexcept
    : descriptor_(std::move(descriptor)), region_(std::move(region)),
      size_classes_(std::move(size_classes)), directory_(std::move(directory)) {}

ChunkViewResult MemoryPoolView::read(const ChunkHandle& handle,
                                     std::size_t max_message_size) const noexcept {
    if (handle.pool_id != descriptor_.pool_id || handle.chunk_index >= directory_.size() ||
        handle.generation == 0U ||
        handle.payload_offset != directory_[handle.chunk_index].payload_offset) {
        return {ErrorCode::InvalidChunkHandle, nullptr, 0, 0, 0};
    }
    const auto* bytes = static_cast<const std::uint8_t*>(region_.data());
    const ChunkDirectoryEntry& entry = directory_[handle.chunk_index];
    const auto* header = reinterpret_cast<const ChunkHeader*>(bytes + entry.header_offset);
    if (header->state.load(std::memory_order_acquire) !=
            static_cast<std::uint32_t>(ChunkState::Published) ||
        header->generation != handle.generation || header->pool_id != descriptor_.pool_id ||
        header->topic_id != descriptor_.topic_id || header->capacity != entry.capacity ||
        header->size_class_index != entry.size_class_index ||
        header->payload_size > header->capacity || header->payload_size > max_message_size ||
        header->ref_count.load(std::memory_order_relaxed) == 0U || header->sequence == 0U) {
        return {ErrorCode::InvalidSharedMemory, nullptr, 0, 0, 0};
    }
    return {ErrorCode::Ok, bytes + entry.payload_offset, header->payload_size, header->sequence,
            header->publish_timestamp_ns};
}

} // namespace mw::detail
