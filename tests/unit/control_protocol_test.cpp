#include "detail/control_protocol.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <stdexcept>

namespace {

TEST(ControlProtocolTest, EncodesAndDecodesHeaderAndPayload) {
    mw::detail::PayloadWriter writer;
    writer.writeU16(0x1234U);
    writer.writeU32(0x12345678U);
    writer.writeU64(0x0102030405060708ULL);
    writer.writeString("camera");

    const auto frame =
        mw::detail::encodeControlFrame(mw::detail::Opcode::AdvertiseTopic, 42U, writer.data());
    ASSERT_EQ(frame.size(), mw::detail::kControlHeaderSize + writer.data().size());
    const auto header =
        mw::detail::decodeControlHeader(frame.data(), mw::detail::kControlHeaderSize);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->magic, mw::detail::kControlMagic);
    EXPECT_EQ(header->version, mw::detail::kControlVersion);
    EXPECT_EQ(header->opcode, mw::detail::Opcode::AdvertiseTopic);
    EXPECT_EQ(header->request_id, 42U);
    EXPECT_EQ(header->payload_size, writer.data().size());

    std::vector<std::uint8_t> payload(frame.begin() + mw::detail::kControlHeaderSize, frame.end());
    mw::detail::PayloadReader reader{payload};
    std::uint16_t value16 = 0;
    std::uint32_t value32 = 0;
    std::uint64_t value64 = 0;
    std::string text;
    EXPECT_TRUE(reader.readU16(value16));
    EXPECT_TRUE(reader.readU32(value32));
    EXPECT_TRUE(reader.readU64(value64));
    EXPECT_TRUE(reader.readString(text));
    EXPECT_EQ(value16, 0x1234U);
    EXPECT_EQ(value32, 0x12345678U);
    EXPECT_EQ(value64, 0x0102030405060708ULL);
    EXPECT_EQ(text, "camera");
    EXPECT_TRUE(reader.empty());
}

TEST(ControlProtocolTest, RejectsMalformedHeadersAndPayloadBounds) {
    const auto encoded = mw::detail::encodeControlHeader({});
    EXPECT_FALSE(mw::detail::decodeControlHeader(encoded.data(), encoded.size() - 1U).has_value());

    auto bad_magic = encoded;
    bad_magic[0] ^= 0xFFU;
    const auto bad_magic_header =
        mw::detail::decodeControlHeader(bad_magic.data(), bad_magic.size());
    ASSERT_TRUE(bad_magic_header.has_value());
    EXPECT_EQ(mw::detail::validateControlHeader(*bad_magic_header),
              mw::detail::ControlHeaderValidation::BadMagic);

    auto bad_version = encoded;
    bad_version[5] = 2U;
    const auto bad_version_header =
        mw::detail::decodeControlHeader(bad_version.data(), bad_version.size());
    ASSERT_TRUE(bad_version_header.has_value());
    EXPECT_EQ(mw::detail::validateControlHeader(*bad_version_header),
              mw::detail::ControlHeaderValidation::UnsupportedVersion);

    mw::detail::ControlHeader oversized{};
    oversized.payload_size = static_cast<std::uint32_t>(mw::detail::kMaxControlPayloadSize + 1U);
    EXPECT_EQ(mw::detail::validateControlHeader(oversized),
              mw::detail::ControlHeaderValidation::PayloadTooLarge);

    EXPECT_THROW((void)mw::detail::encodeControlFrame(
                     mw::detail::Opcode::RegisterNode, 1U,
                     std::vector<std::uint8_t>(mw::detail::kMaxControlPayloadSize + 1U)),
                 std::length_error);
}

TEST(ControlProtocolTest, DecodesResponseEnvelopeAndRejectsTruncatedString) {
    const std::vector<std::uint8_t> body{1U, 2U, 3U};
    const auto encoded =
        mw::detail::encodeResponsePayload(mw::ErrorCode::TypeMismatch, "different type", body);
    const auto decoded = mw::detail::decodeResponsePayload(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->error, mw::ErrorCode::TypeMismatch);
    EXPECT_EQ(decoded->message, "different type");
    EXPECT_EQ(decoded->body, body);

    const std::vector<std::uint8_t> invalid_string{0U, 0U, 0U, 10U, 'x'};
    EXPECT_FALSE(mw::detail::decodeResponsePayload(invalid_string).has_value());
}

} // namespace
