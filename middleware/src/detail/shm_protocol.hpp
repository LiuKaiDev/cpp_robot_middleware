#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace mw::detail {

inline constexpr std::uint32_t kSharedMemoryMagic = 0x4D575333U;
inline constexpr std::uint16_t kSharedMemoryVersion = 1U;
inline constexpr std::size_t kSharedMemoryHeaderSize = 48U;
inline constexpr std::uint32_t kShmNotificationMagic = 0x4D574E33U;
inline constexpr std::uint16_t kShmNotificationVersion = 1U;
inline constexpr std::size_t kMaxShmNameSize = 192U;
inline constexpr std::size_t kShmNotificationHeaderSize = 56U;
inline constexpr std::size_t kShmNotificationSize = kShmNotificationHeaderSize + kMaxShmNameSize;
inline constexpr std::size_t kShmAckSize = 16U;

enum class ShmFrameKind : std::uint16_t {
    Notification = 1,
    Ack = 2,
};

struct SharedMemoryHeader {
    std::uint32_t magic{kSharedMemoryMagic};
    std::uint16_t version{kSharedMemoryVersion};
    std::uint16_t header_size{kSharedMemoryHeaderSize};
    std::uint64_t segment_size{0};
    std::uint64_t payload_size{0};
    std::uint64_t sequence{0};
    std::uint64_t publish_timestamp_ns{0};
    std::uint64_t topic_id{0};
};

struct ShmMessageHandle {
    std::string shm_name;
    std::uint64_t segment_size{0};
    std::uint64_t payload_size{0};
    std::uint64_t sequence{0};
    std::uint64_t publish_timestamp_ns{0};
    std::uint64_t topic_id{0};
};

struct ShmNotification {
    std::uint32_t magic{kShmNotificationMagic};
    std::uint16_t version{kShmNotificationVersion};
    ShmFrameKind kind{ShmFrameKind::Notification};
    ShmMessageHandle handle;
};

struct ShmAck {
    std::uint32_t magic{kShmNotificationMagic};
    std::uint16_t version{kShmNotificationVersion};
    ShmFrameKind kind{ShmFrameKind::Ack};
    std::uint64_t sequence{0};
};

enum class ShmValidation {
    Valid,
    BadMagic,
    UnsupportedVersion,
    BadKind,
    InvalidName,
    InvalidSegmentSize,
    PayloadTooLarge,
    MetadataMismatch,
};

std::array<std::uint8_t, kSharedMemoryHeaderSize>
encodeSharedMemoryHeader(const SharedMemoryHeader& header) noexcept;
std::optional<SharedMemoryHeader> decodeSharedMemoryHeader(const std::uint8_t* data,
                                                           std::size_t size) noexcept;
ShmValidation validateSharedMemoryHeader(const SharedMemoryHeader& header, std::size_t mapped_size,
                                         std::size_t max_message_size) noexcept;

std::optional<std::array<std::uint8_t, kShmNotificationSize>>
encodeShmNotification(const ShmNotification& notification) noexcept;
std::optional<ShmNotification> decodeShmNotification(const std::uint8_t* data,
                                                     std::size_t size) noexcept;
ShmValidation validateShmNotification(const ShmNotification& notification,
                                      std::size_t max_message_size,
                                      std::uint64_t expected_topic_id) noexcept;

std::array<std::uint8_t, kShmAckSize> encodeShmAck(const ShmAck& ack) noexcept;
std::optional<ShmAck> decodeShmAck(const std::uint8_t* data, std::size_t size) noexcept;
ShmValidation validateShmAck(const ShmAck& ack, std::uint64_t expected_sequence) noexcept;

bool sharedMemoryMetadataMatches(const SharedMemoryHeader& header,
                                 const ShmMessageHandle& handle) noexcept;

} // namespace mw::detail
