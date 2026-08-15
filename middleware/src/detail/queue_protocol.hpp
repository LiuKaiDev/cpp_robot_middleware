#pragma once

#include <mw/config.hpp>
#include <mw/result.hpp>

#include "detail/pool_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace mw::detail {

inline constexpr std::uint32_t kQueueProtocolMagic = 0x4D575135U;
inline constexpr std::uint16_t kQueueProtocolVersion = 1U;
inline constexpr std::uint16_t kQueueLayoutVersion = 1U;
inline constexpr std::size_t kMaxQueueNameSize = 192U;
inline constexpr std::size_t kQueueWakeHeaderSize = 80U;
inline constexpr std::size_t kQueueWakeSize = kQueueWakeHeaderSize + kMaxPoolNameSize;
inline constexpr std::size_t kQueueReleaseSize = 32U;

enum class QueueFrameKind : std::uint16_t {
    Wake = 1,
    Release = 2,
};

struct QueueDescriptor {
    std::string shm_name;
    std::uint64_t queue_id{0};
    std::uint64_t segment_size{0};
    std::uint32_t capacity{0};
    std::uint16_t layout_version{0};
    OverflowPolicy overflow_policy{static_cast<OverflowPolicy>(0)};
    std::uint64_t block_timeout_ms{0};
};

bool operator==(const QueueDescriptor& left, const QueueDescriptor& right) noexcept;
bool operator!=(const QueueDescriptor& left, const QueueDescriptor& right) noexcept;

struct QueueWake {
    PoolDescriptor pool;
    std::uint64_t queue_id{0};
};

std::optional<std::array<std::uint8_t, kQueueWakeSize>>
encodeQueueWake(const QueueWake& wake) noexcept;
std::optional<QueueWake> decodeQueueWake(const std::uint8_t* data, std::size_t size) noexcept;
ErrorCode validateQueueWake(const QueueWake& wake, std::uint64_t expected_queue_id,
                            std::uint64_t expected_topic_id) noexcept;

std::array<std::uint8_t, kQueueReleaseSize>
encodeQueueRelease(const ChunkHandle& handle) noexcept;
std::optional<ChunkHandle> decodeQueueRelease(const std::uint8_t* data,
                                              std::size_t size) noexcept;

bool validOverflowPolicy(OverflowPolicy policy) noexcept;

} // namespace mw::detail
