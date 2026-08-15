#include "detail/queue_protocol.hpp"

#include "detail/shared_memory.hpp"

#include <algorithm>
#include <limits>

namespace mw::detail {
namespace {

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

} // namespace

bool operator==(const QueueDescriptor& left, const QueueDescriptor& right) noexcept {
    return left.shm_name == right.shm_name && left.queue_id == right.queue_id &&
           left.segment_size == right.segment_size && left.capacity == right.capacity &&
           left.layout_version == right.layout_version &&
           left.overflow_policy == right.overflow_policy &&
           left.block_timeout_ms == right.block_timeout_ms;
}

bool operator!=(const QueueDescriptor& left, const QueueDescriptor& right) noexcept {
    return !(left == right);
}

bool validOverflowPolicy(OverflowPolicy policy) noexcept {
    return policy == OverflowPolicy::DropNewest || policy == OverflowPolicy::DropOldest ||
           policy == OverflowPolicy::BlockWithTimeout;
}

std::optional<std::array<std::uint8_t, kQueueWakeSize>>
encodeQueueWake(const QueueWake& wake) noexcept {
    if (!isValidSharedMemoryName(wake.pool.shm_name) ||
        wake.pool.shm_name.size() > kMaxPoolNameSize || wake.queue_id == 0U) {
        return std::nullopt;
    }

    std::array<std::uint8_t, kQueueWakeSize> encoded{};
    encodeBigEndian(kQueueProtocolMagic, encoded.data());
    encodeBigEndian(kQueueProtocolVersion, encoded.data() + 4U);
    encodeBigEndian(static_cast<std::uint16_t>(QueueFrameKind::Wake), encoded.data() + 6U);
    encodeBigEndian(static_cast<std::uint32_t>(kQueueWakeSize), encoded.data() + 8U);
    encodeBigEndian(static_cast<std::uint16_t>(wake.pool.shm_name.size()), encoded.data() + 12U);
    encodeBigEndian(wake.pool.layout_version, encoded.data() + 14U);
    encodeBigEndian(wake.pool.segment_size, encoded.data() + 16U);
    encodeBigEndian(wake.pool.pool_id, encoded.data() + 24U);
    encodeBigEndian(wake.pool.topic_id, encoded.data() + 32U);
    encodeBigEndian(wake.queue_id, encoded.data() + 40U);
    std::copy(wake.pool.shm_name.begin(), wake.pool.shm_name.end(),
              encoded.begin() + static_cast<std::ptrdiff_t>(kQueueWakeHeaderSize));
    return encoded;
}

std::optional<QueueWake> decodeQueueWake(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr || size != kQueueWakeSize ||
        decodeBigEndian<std::uint32_t>(data) != kQueueProtocolMagic ||
        decodeBigEndian<std::uint16_t>(data + 4U) != kQueueProtocolVersion ||
        decodeBigEndian<std::uint16_t>(data + 6U) !=
            static_cast<std::uint16_t>(QueueFrameKind::Wake) ||
        decodeBigEndian<std::uint32_t>(data + 8U) != kQueueWakeSize) {
        return std::nullopt;
    }
    const std::uint16_t name_size = decodeBigEndian<std::uint16_t>(data + 12U);
    if (name_size == 0U || name_size > kMaxPoolNameSize) {
        return std::nullopt;
    }
    for (std::size_t index = kQueueWakeHeaderSize + name_size; index < size; ++index) {
        if (data[index] != 0U) {
            return std::nullopt;
        }
    }

    QueueWake wake;
    wake.pool.shm_name.assign(reinterpret_cast<const char*>(data + kQueueWakeHeaderSize),
                              name_size);
    wake.pool.layout_version = decodeBigEndian<std::uint16_t>(data + 14U);
    wake.pool.segment_size = decodeBigEndian<std::uint64_t>(data + 16U);
    wake.pool.pool_id = decodeBigEndian<std::uint64_t>(data + 24U);
    wake.pool.topic_id = decodeBigEndian<std::uint64_t>(data + 32U);
    wake.queue_id = decodeBigEndian<std::uint64_t>(data + 40U);
    return wake;
}

ErrorCode validateQueueWake(const QueueWake& wake, std::uint64_t expected_queue_id,
                            std::uint64_t expected_topic_id) noexcept {
    if (!isValidSharedMemoryName(wake.pool.shm_name) ||
        wake.pool.layout_version != kPoolLayoutVersion || wake.pool.segment_size == 0U ||
        wake.pool.segment_size > std::numeric_limits<std::size_t>::max() ||
        wake.pool.pool_id == 0U || wake.pool.topic_id != expected_topic_id ||
        wake.queue_id == 0U || wake.queue_id != expected_queue_id) {
        return ErrorCode::InvalidFrame;
    }
    return ErrorCode::Ok;
}

std::array<std::uint8_t, kQueueReleaseSize>
encodeQueueRelease(const ChunkHandle& handle) noexcept {
    std::array<std::uint8_t, kQueueReleaseSize> encoded{};
    encodeBigEndian(kQueueProtocolMagic, encoded.data());
    encodeBigEndian(kQueueProtocolVersion, encoded.data() + 4U);
    encodeBigEndian(static_cast<std::uint16_t>(QueueFrameKind::Release), encoded.data() + 6U);
    encodeBigEndian(handle.pool_id, encoded.data() + 8U);
    encodeBigEndian(handle.chunk_index, encoded.data() + 16U);
    encodeBigEndian(handle.generation, encoded.data() + 20U);
    encodeBigEndian(handle.payload_offset, encoded.data() + 24U);
    return encoded;
}

std::optional<ChunkHandle> decodeQueueRelease(const std::uint8_t* data,
                                              std::size_t size) noexcept {
    if (data == nullptr || size != kQueueReleaseSize ||
        decodeBigEndian<std::uint32_t>(data) != kQueueProtocolMagic ||
        decodeBigEndian<std::uint16_t>(data + 4U) != kQueueProtocolVersion ||
        decodeBigEndian<std::uint16_t>(data + 6U) !=
            static_cast<std::uint16_t>(QueueFrameKind::Release)) {
        return std::nullopt;
    }
    ChunkHandle handle{decodeBigEndian<std::uint64_t>(data + 8U),
                       decodeBigEndian<std::uint32_t>(data + 16U),
                       decodeBigEndian<std::uint32_t>(data + 20U),
                       decodeBigEndian<std::uint64_t>(data + 24U)};
    if (handle.pool_id == 0U || handle.generation == 0U) {
        return std::nullopt;
    }
    return handle;
}

} // namespace mw::detail
