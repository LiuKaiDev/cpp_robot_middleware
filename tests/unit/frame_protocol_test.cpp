#include "detail/frame_protocol.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

TEST(FrameProtocolTest, EncodesAndDecodesEveryField) {
    const mw::detail::FrameHeader header{
        mw::detail::kFrameMagic,
        0x01020304U,
        0x0102030405060708ULL,
        0x1112131415161718ULL,
    };

    const auto encoded = mw::detail::encodeFrameHeader(header);
    const std::array<std::uint8_t, 8> expected_prefix{'M', 'W', '0', '1', 1, 2, 3, 4};
    EXPECT_TRUE(std::equal(expected_prefix.begin(), expected_prefix.end(), encoded.begin()));

    const auto decoded = mw::detail::decodeFrameHeader(encoded.data(), encoded.size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->magic, header.magic);
    EXPECT_EQ(decoded->payload_size, header.payload_size);
    EXPECT_EQ(decoded->sequence, header.sequence);
    EXPECT_EQ(decoded->timestamp_ns, header.timestamp_ns);
}

TEST(FrameProtocolTest, RejectsWrongEncodedSize) {
    const std::array<std::uint8_t, mw::detail::kFrameHeaderSize - 1U> truncated{};
    EXPECT_FALSE(mw::detail::decodeFrameHeader(truncated.data(), truncated.size()).has_value());
    EXPECT_FALSE(mw::detail::decodeFrameHeader(nullptr, mw::detail::kFrameHeaderSize).has_value());
}

TEST(FrameProtocolTest, ValidatesMagicAndPayloadLimit) {
    mw::detail::FrameHeader header;
    header.payload_size = 1024;
    EXPECT_EQ(mw::detail::validateFrameHeader(header, 1024), mw::detail::FrameValidation::Valid);

    header.magic = 0;
    EXPECT_EQ(mw::detail::validateFrameHeader(header, 1024), mw::detail::FrameValidation::BadMagic);

    header.magic = mw::detail::kFrameMagic;
    EXPECT_EQ(mw::detail::validateFrameHeader(header, 1023),
              mw::detail::FrameValidation::PayloadTooLarge);
}

} // namespace
