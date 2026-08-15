#include "detail/shm_protocol.hpp"

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

bool checkedSegmentSize(std::uint64_t payload_size, std::uint64_t& segment_size) noexcept {
    if (payload_size > std::numeric_limits<std::uint64_t>::max() - kSharedMemoryHeaderSize) {
        return false;
    }
    segment_size = payload_size + kSharedMemoryHeaderSize;
    return true;
}

} // namespace

std::array<std::uint8_t, kSharedMemoryHeaderSize>
encodeSharedMemoryHeader(const SharedMemoryHeader& header) noexcept {
    std::array<std::uint8_t, kSharedMemoryHeaderSize> encoded{};
    encodeBigEndian(header.magic, encoded.data());
    encodeBigEndian(header.version, encoded.data() + 4U);
    encodeBigEndian(header.header_size, encoded.data() + 6U);
    encodeBigEndian(header.segment_size, encoded.data() + 8U);
    encodeBigEndian(header.payload_size, encoded.data() + 16U);
    encodeBigEndian(header.sequence, encoded.data() + 24U);
    encodeBigEndian(header.publish_timestamp_ns, encoded.data() + 32U);
    encodeBigEndian(header.topic_id, encoded.data() + 40U);
    return encoded;
}

std::optional<SharedMemoryHeader> decodeSharedMemoryHeader(const std::uint8_t* data,
                                                           std::size_t size) noexcept {
    if (data == nullptr || size != kSharedMemoryHeaderSize) {
        return std::nullopt;
    }
    return SharedMemoryHeader{
        decodeBigEndian<std::uint32_t>(data),       decodeBigEndian<std::uint16_t>(data + 4U),
        decodeBigEndian<std::uint16_t>(data + 6U),  decodeBigEndian<std::uint64_t>(data + 8U),
        decodeBigEndian<std::uint64_t>(data + 16U), decodeBigEndian<std::uint64_t>(data + 24U),
        decodeBigEndian<std::uint64_t>(data + 32U), decodeBigEndian<std::uint64_t>(data + 40U),
    };
}

ShmValidation validateSharedMemoryHeader(const SharedMemoryHeader& header, std::size_t mapped_size,
                                         std::size_t max_message_size) noexcept {
    if (header.magic != kSharedMemoryMagic) {
        return ShmValidation::BadMagic;
    }
    if (header.version != kSharedMemoryVersion) {
        return ShmValidation::UnsupportedVersion;
    }
    if (header.header_size != kSharedMemoryHeaderSize || header.segment_size != mapped_size ||
        header.segment_size < header.header_size) {
        return ShmValidation::InvalidSegmentSize;
    }
    std::uint64_t expected_segment_size = 0;
    if (!checkedSegmentSize(header.payload_size, expected_segment_size) ||
        expected_segment_size != header.segment_size) {
        return ShmValidation::InvalidSegmentSize;
    }
    if (header.payload_size > max_message_size) {
        return ShmValidation::PayloadTooLarge;
    }
    return ShmValidation::Valid;
}

std::optional<std::array<std::uint8_t, kShmNotificationSize>>
encodeShmNotification(const ShmNotification& notification) noexcept {
    if (!isValidSharedMemoryName(notification.handle.shm_name) ||
        notification.handle.shm_name.size() > kMaxShmNameSize) {
        return std::nullopt;
    }
    std::array<std::uint8_t, kShmNotificationSize> encoded{};
    encodeBigEndian(notification.magic, encoded.data());
    encodeBigEndian(notification.version, encoded.data() + 4U);
    encodeBigEndian(static_cast<std::uint16_t>(notification.kind), encoded.data() + 6U);
    encodeBigEndian(static_cast<std::uint16_t>(notification.handle.shm_name.size()),
                    encoded.data() + 8U);
    encodeBigEndian<std::uint16_t>(0U, encoded.data() + 10U);
    encodeBigEndian(static_cast<std::uint32_t>(kShmNotificationSize), encoded.data() + 12U);
    encodeBigEndian(notification.handle.segment_size, encoded.data() + 16U);
    encodeBigEndian(notification.handle.payload_size, encoded.data() + 24U);
    encodeBigEndian(notification.handle.sequence, encoded.data() + 32U);
    encodeBigEndian(notification.handle.publish_timestamp_ns, encoded.data() + 40U);
    encodeBigEndian(notification.handle.topic_id, encoded.data() + 48U);
    std::copy(notification.handle.shm_name.begin(), notification.handle.shm_name.end(),
              encoded.begin() + static_cast<std::ptrdiff_t>(kShmNotificationHeaderSize));
    return encoded;
}

std::optional<ShmNotification> decodeShmNotification(const std::uint8_t* data,
                                                     std::size_t size) noexcept {
    if (data == nullptr || size != kShmNotificationSize) {
        return std::nullopt;
    }
    const std::uint16_t name_size = decodeBigEndian<std::uint16_t>(data + 8U);
    const std::uint16_t reserved = decodeBigEndian<std::uint16_t>(data + 10U);
    const std::uint32_t frame_size = decodeBigEndian<std::uint32_t>(data + 12U);
    if (name_size == 0U || name_size > kMaxShmNameSize || reserved != 0U ||
        frame_size != kShmNotificationSize) {
        return std::nullopt;
    }
    for (std::size_t index = kShmNotificationHeaderSize + name_size; index < size; ++index) {
        if (data[index] != 0U) {
            return std::nullopt;
        }
    }
    ShmNotification notification;
    notification.magic = decodeBigEndian<std::uint32_t>(data);
    notification.version = decodeBigEndian<std::uint16_t>(data + 4U);
    notification.kind = static_cast<ShmFrameKind>(decodeBigEndian<std::uint16_t>(data + 6U));
    notification.handle.shm_name.assign(
        reinterpret_cast<const char*>(data + kShmNotificationHeaderSize), name_size);
    notification.handle.segment_size = decodeBigEndian<std::uint64_t>(data + 16U);
    notification.handle.payload_size = decodeBigEndian<std::uint64_t>(data + 24U);
    notification.handle.sequence = decodeBigEndian<std::uint64_t>(data + 32U);
    notification.handle.publish_timestamp_ns = decodeBigEndian<std::uint64_t>(data + 40U);
    notification.handle.topic_id = decodeBigEndian<std::uint64_t>(data + 48U);
    return notification;
}

ShmValidation validateShmNotification(const ShmNotification& notification,
                                      std::size_t max_message_size,
                                      std::uint64_t expected_topic_id) noexcept {
    if (notification.magic != kShmNotificationMagic) {
        return ShmValidation::BadMagic;
    }
    if (notification.version != kShmNotificationVersion) {
        return ShmValidation::UnsupportedVersion;
    }
    if (notification.kind != ShmFrameKind::Notification) {
        return ShmValidation::BadKind;
    }
    if (!isValidSharedMemoryName(notification.handle.shm_name)) {
        return ShmValidation::InvalidName;
    }
    std::uint64_t expected_segment_size = 0;
    if (!checkedSegmentSize(notification.handle.payload_size, expected_segment_size) ||
        expected_segment_size != notification.handle.segment_size ||
        notification.handle.segment_size > std::numeric_limits<std::size_t>::max()) {
        return ShmValidation::InvalidSegmentSize;
    }
    if (notification.handle.payload_size > max_message_size) {
        return ShmValidation::PayloadTooLarge;
    }
    if (notification.handle.sequence == 0U || notification.handle.topic_id != expected_topic_id) {
        return ShmValidation::MetadataMismatch;
    }
    return ShmValidation::Valid;
}

std::array<std::uint8_t, kShmAckSize> encodeShmAck(const ShmAck& ack) noexcept {
    std::array<std::uint8_t, kShmAckSize> encoded{};
    encodeBigEndian(ack.magic, encoded.data());
    encodeBigEndian(ack.version, encoded.data() + 4U);
    encodeBigEndian(static_cast<std::uint16_t>(ack.kind), encoded.data() + 6U);
    encodeBigEndian(ack.sequence, encoded.data() + 8U);
    return encoded;
}

std::optional<ShmAck> decodeShmAck(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr || size != kShmAckSize) {
        return std::nullopt;
    }
    return ShmAck{
        decodeBigEndian<std::uint32_t>(data),
        decodeBigEndian<std::uint16_t>(data + 4U),
        static_cast<ShmFrameKind>(decodeBigEndian<std::uint16_t>(data + 6U)),
        decodeBigEndian<std::uint64_t>(data + 8U),
    };
}

ShmValidation validateShmAck(const ShmAck& ack, std::uint64_t expected_sequence) noexcept {
    if (ack.magic != kShmNotificationMagic) {
        return ShmValidation::BadMagic;
    }
    if (ack.version != kShmNotificationVersion) {
        return ShmValidation::UnsupportedVersion;
    }
    if (ack.kind != ShmFrameKind::Ack) {
        return ShmValidation::BadKind;
    }
    if (ack.sequence != expected_sequence) {
        return ShmValidation::MetadataMismatch;
    }
    return ShmValidation::Valid;
}

bool sharedMemoryMetadataMatches(const SharedMemoryHeader& header,
                                 const ShmMessageHandle& handle) noexcept {
    return header.segment_size == handle.segment_size &&
           header.payload_size == handle.payload_size && header.sequence == handle.sequence &&
           header.publish_timestamp_ns == handle.publish_timestamp_ns &&
           header.topic_id == handle.topic_id;
}

} // namespace mw::detail
