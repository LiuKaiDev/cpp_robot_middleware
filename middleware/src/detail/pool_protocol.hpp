#pragma once

#include <mw/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace mw::detail {

inline constexpr std::uint32_t kPoolNotificationMagic = 0x4D575034U;
inline constexpr std::uint16_t kPoolNotificationVersion = 1U;
inline constexpr std::uint16_t kPoolLayoutVersion = 1U;
inline constexpr std::size_t kMaxPoolNameSize = 192U;
inline constexpr std::size_t kPoolNotificationHeaderSize = 80U;
inline constexpr std::size_t kPoolNotificationSize = kPoolNotificationHeaderSize + kMaxPoolNameSize;
inline constexpr std::size_t kPoolReleaseSize = 40U;

enum class PoolFrameKind : std::uint16_t {
    Notification = 1,
    Release = 2,
};

struct PoolDescriptor {
    std::string shm_name;
    std::uint64_t pool_id{0};
    std::uint64_t segment_size{0};
    std::uint64_t topic_id{0};
    std::uint16_t layout_version{0};
};

struct ChunkHandle {
    std::uint64_t pool_id{0};
    std::uint32_t chunk_index{0};
    std::uint32_t generation{0};
    std::uint64_t payload_offset{0};
};

bool operator==(const ChunkHandle& left, const ChunkHandle& right) noexcept;
bool operator!=(const ChunkHandle& left, const ChunkHandle& right) noexcept;

struct PoolNotification {
    PoolDescriptor pool;
    ChunkHandle handle;
    std::uint64_t payload_size{0};
    std::uint64_t sequence{0};
    std::uint64_t publish_timestamp_ns{0};
};

struct PoolRelease {
    ChunkHandle handle;
    std::uint64_t sequence{0};
};

std::optional<std::array<std::uint8_t, kPoolNotificationSize>>
encodePoolNotification(const PoolNotification& notification) noexcept;
std::optional<PoolNotification> decodePoolNotification(const std::uint8_t* data,
                                                       std::size_t size) noexcept;
ErrorCode validatePoolNotification(const PoolNotification& notification,
                                   std::size_t max_message_size,
                                   std::uint64_t expected_topic_id) noexcept;

std::array<std::uint8_t, kPoolReleaseSize> encodePoolRelease(const PoolRelease& release) noexcept;
std::optional<PoolRelease> decodePoolRelease(const std::uint8_t* data, std::size_t size) noexcept;
ErrorCode validatePoolRelease(const PoolRelease& release, const ChunkHandle& expected_handle,
                              std::uint64_t expected_sequence) noexcept;

} // namespace mw::detail
