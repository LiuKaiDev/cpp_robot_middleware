#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mw::detail {

inline constexpr std::uint32_t kFrameMagic = 0x4D573031U;
inline constexpr std::size_t kFrameHeaderSize = 24U;

struct FrameHeader {
    std::uint32_t magic{kFrameMagic};
    std::uint32_t payload_size{0};
    std::uint64_t sequence{0};
    std::uint64_t timestamp_ns{0};
};

enum class FrameValidation {
    Valid,
    BadMagic,
    PayloadTooLarge,
};

std::array<std::uint8_t, kFrameHeaderSize> encodeFrameHeader(const FrameHeader& header) noexcept;
std::optional<FrameHeader> decodeFrameHeader(const std::uint8_t* data, std::size_t size) noexcept;
FrameValidation validateFrameHeader(const FrameHeader& header,
                                    std::size_t max_message_size) noexcept;

} // namespace mw::detail
