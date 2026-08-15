#include "detail/pool_protocol.hpp"

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

bool operator==(const ChunkHandle& left, const ChunkHandle& right) noexcept {
    return left.pool_id == right.pool_id && left.chunk_index == right.chunk_index &&
           left.generation == right.generation && left.payload_offset == right.payload_offset;
}

bool operator!=(const ChunkHandle& left, const ChunkHandle& right) noexcept {
    return !(left == right);
}

std::optional<std::array<std::uint8_t, kPoolNotificationSize>>
encodePoolNotification(const PoolNotification& notification) noexcept {
    if (!isValidSharedMemoryName(notification.pool.shm_name) ||
        notification.pool.shm_name.size() > kMaxPoolNameSize) {
        return std::nullopt;
    }

    std::array<std::uint8_t, kPoolNotificationSize> encoded{};
    encodeBigEndian(kPoolNotificationMagic, encoded.data());
    encodeBigEndian(kPoolNotificationVersion, encoded.data() + 4U);
    encodeBigEndian(static_cast<std::uint16_t>(PoolFrameKind::Notification), encoded.data() + 6U);
    encodeBigEndian(static_cast<std::uint32_t>(kPoolNotificationSize), encoded.data() + 8U);
    encodeBigEndian(static_cast<std::uint16_t>(notification.pool.shm_name.size()),
                    encoded.data() + 12U);
    encodeBigEndian(notification.pool.layout_version, encoded.data() + 14U);
    encodeBigEndian(notification.pool.segment_size, encoded.data() + 16U);
    encodeBigEndian(notification.pool.pool_id, encoded.data() + 24U);
    encodeBigEndian(notification.pool.topic_id, encoded.data() + 32U);
    encodeBigEndian(notification.handle.chunk_index, encoded.data() + 40U);
    encodeBigEndian(notification.handle.generation, encoded.data() + 44U);
    encodeBigEndian(notification.handle.payload_offset, encoded.data() + 48U);
    encodeBigEndian(notification.payload_size, encoded.data() + 56U);
    encodeBigEndian(notification.sequence, encoded.data() + 64U);
    encodeBigEndian(notification.publish_timestamp_ns, encoded.data() + 72U);
    std::copy(notification.pool.shm_name.begin(), notification.pool.shm_name.end(),
              encoded.begin() + static_cast<std::ptrdiff_t>(kPoolNotificationHeaderSize));
    return encoded;
}

std::optional<PoolNotification> decodePoolNotification(const std::uint8_t* data,
                                                       std::size_t size) noexcept {
    if (data == nullptr || size != kPoolNotificationSize) {
        return std::nullopt;
    }
    const std::uint32_t frame_size = decodeBigEndian<std::uint32_t>(data + 8U);
    const std::uint16_t name_size = decodeBigEndian<std::uint16_t>(data + 12U);
    if (frame_size != kPoolNotificationSize || name_size == 0U || name_size > kMaxPoolNameSize) {
        return std::nullopt;
    }
    for (std::size_t index = kPoolNotificationHeaderSize + name_size; index < size; ++index) {
        if (data[index] != 0U) {
            return std::nullopt;
        }
    }
    if (decodeBigEndian<std::uint32_t>(data) != kPoolNotificationMagic ||
        decodeBigEndian<std::uint16_t>(data + 4U) != kPoolNotificationVersion ||
        decodeBigEndian<std::uint16_t>(data + 6U) !=
            static_cast<std::uint16_t>(PoolFrameKind::Notification)) {
        return std::nullopt;
    }

    PoolNotification notification;
    notification.pool.shm_name.assign(
        reinterpret_cast<const char*>(data + kPoolNotificationHeaderSize), name_size);
    notification.pool.layout_version = decodeBigEndian<std::uint16_t>(data + 14U);
    notification.pool.segment_size = decodeBigEndian<std::uint64_t>(data + 16U);
    notification.pool.pool_id = decodeBigEndian<std::uint64_t>(data + 24U);
    notification.pool.topic_id = decodeBigEndian<std::uint64_t>(data + 32U);
    notification.handle.pool_id = notification.pool.pool_id;
    notification.handle.chunk_index = decodeBigEndian<std::uint32_t>(data + 40U);
    notification.handle.generation = decodeBigEndian<std::uint32_t>(data + 44U);
    notification.handle.payload_offset = decodeBigEndian<std::uint64_t>(data + 48U);
    notification.payload_size = decodeBigEndian<std::uint64_t>(data + 56U);
    notification.sequence = decodeBigEndian<std::uint64_t>(data + 64U);
    notification.publish_timestamp_ns = decodeBigEndian<std::uint64_t>(data + 72U);
    return notification;
}

ErrorCode validatePoolNotification(const PoolNotification& notification,
                                   std::size_t max_message_size,
                                   std::uint64_t expected_topic_id) noexcept {
    if (!isValidSharedMemoryName(notification.pool.shm_name) ||
        notification.pool.layout_version != kPoolLayoutVersion ||
        notification.pool.segment_size == 0U ||
        notification.pool.segment_size > std::numeric_limits<std::size_t>::max() ||
        notification.pool.pool_id == 0U ||
        notification.handle.pool_id != notification.pool.pool_id ||
        notification.handle.generation == 0U || notification.sequence == 0U ||
        notification.pool.topic_id != expected_topic_id) {
        return ErrorCode::InvalidFrame;
    }
    if (notification.payload_size > max_message_size) {
        return ErrorCode::MessageTooLarge;
    }
    return ErrorCode::Ok;
}

std::array<std::uint8_t, kPoolReleaseSize> encodePoolRelease(const PoolRelease& release) noexcept {
    std::array<std::uint8_t, kPoolReleaseSize> encoded{};
    encodeBigEndian(kPoolNotificationMagic, encoded.data());
    encodeBigEndian(kPoolNotificationVersion, encoded.data() + 4U);
    encodeBigEndian(static_cast<std::uint16_t>(PoolFrameKind::Release), encoded.data() + 6U);
    encodeBigEndian(release.handle.pool_id, encoded.data() + 8U);
    encodeBigEndian(release.handle.chunk_index, encoded.data() + 16U);
    encodeBigEndian(release.handle.generation, encoded.data() + 20U);
    encodeBigEndian(release.handle.payload_offset, encoded.data() + 24U);
    encodeBigEndian(release.sequence, encoded.data() + 32U);
    return encoded;
}

std::optional<PoolRelease> decodePoolRelease(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr || size != kPoolReleaseSize ||
        decodeBigEndian<std::uint32_t>(data) != kPoolNotificationMagic ||
        decodeBigEndian<std::uint16_t>(data + 4U) != kPoolNotificationVersion ||
        decodeBigEndian<std::uint16_t>(data + 6U) !=
            static_cast<std::uint16_t>(PoolFrameKind::Release)) {
        return std::nullopt;
    }
    return PoolRelease{
        {decodeBigEndian<std::uint64_t>(data + 8U), decodeBigEndian<std::uint32_t>(data + 16U),
         decodeBigEndian<std::uint32_t>(data + 20U), decodeBigEndian<std::uint64_t>(data + 24U)},
        decodeBigEndian<std::uint64_t>(data + 32U)};
}

ErrorCode validatePoolRelease(const PoolRelease& release, const ChunkHandle& expected_handle,
                              std::uint64_t expected_sequence) noexcept {
    if (release.handle != expected_handle || release.sequence != expected_sequence) {
        return ErrorCode::InvalidChunkHandle;
    }
    return ErrorCode::Ok;
}

} // namespace mw::detail
