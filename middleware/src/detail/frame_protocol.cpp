#include "detail/frame_protocol.hpp"

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

std::array<std::uint8_t, kFrameHeaderSize> encodeFrameHeader(const FrameHeader& header) noexcept {
    std::array<std::uint8_t, kFrameHeaderSize> encoded{};
    encodeBigEndian(header.magic, encoded.data());
    encodeBigEndian(header.payload_size, encoded.data() + 4U);
    encodeBigEndian(header.sequence, encoded.data() + 8U);
    encodeBigEndian(header.timestamp_ns, encoded.data() + 16U);
    return encoded;
}

std::optional<FrameHeader> decodeFrameHeader(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr || size != kFrameHeaderSize) {
        return std::nullopt;
    }

    return FrameHeader{
        decodeBigEndian<std::uint32_t>(data),
        decodeBigEndian<std::uint32_t>(data + 4U),
        decodeBigEndian<std::uint64_t>(data + 8U),
        decodeBigEndian<std::uint64_t>(data + 16U),
    };
}

FrameValidation validateFrameHeader(const FrameHeader& header,
                                    std::size_t max_message_size) noexcept {
    if (header.magic != kFrameMagic) {
        return FrameValidation::BadMagic;
    }
    if (header.payload_size > max_message_size) {
        return FrameValidation::PayloadTooLarge;
    }
    return FrameValidation::Valid;
}

} // namespace mw::detail
