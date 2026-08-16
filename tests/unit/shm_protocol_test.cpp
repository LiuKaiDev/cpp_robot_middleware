#include "detail/shm_protocol.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

mw::detail::ShmNotification makeNotification(std::uint64_t payload_size = 1024U) {
    mw::detail::ShmNotification notification;
    notification.handle = {"/mw_shm_protocol_test",
                           mw::detail::kSharedMemoryHeaderSize + payload_size,
                           payload_size,
                           7U,
                           123456U,
                           99U};
    return notification;
}

TEST(ShmProtocolTest, EncodesAndValidatesSegmentHeader) {
    const mw::detail::SharedMemoryHeader header{
        mw::detail::kSharedMemoryMagic,
        mw::detail::kSharedMemoryVersion,
        static_cast<std::uint16_t>(mw::detail::kSharedMemoryHeaderSize),
        1072U,
        1024U,
        7U,
        123456U,
        99U};
    const auto encoded = mw::detail::encodeSharedMemoryHeader(header);
    const auto decoded = mw::detail::decodeSharedMemoryHeader(encoded.data(), encoded.size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->segment_size, 1072U);
    EXPECT_EQ(decoded->payload_size, 1024U);
    EXPECT_EQ(decoded->sequence, 7U);
    EXPECT_EQ(decoded->publish_timestamp_ns, 123456U);
    EXPECT_EQ(decoded->topic_id, 99U);
    EXPECT_EQ(mw::detail::validateSharedMemoryHeader(*decoded, 1072U, 1024U),
              mw::detail::ShmValidation::Valid);
}

TEST(ShmProtocolTest, RejectsBadSegmentMetadataAndBounds) {
    mw::detail::SharedMemoryHeader header;
    header.segment_size = mw::detail::kSharedMemoryHeaderSize;
    header.sequence = 1U;
    header.magic = 0U;
    EXPECT_EQ(mw::detail::validateSharedMemoryHeader(header, header.segment_size, 1024U),
              mw::detail::ShmValidation::BadMagic);
    header.magic = mw::detail::kSharedMemoryMagic;
    header.version = 99U;
    EXPECT_EQ(mw::detail::validateSharedMemoryHeader(header, header.segment_size, 1024U),
              mw::detail::ShmValidation::UnsupportedVersion);
    header.version = mw::detail::kSharedMemoryVersion;
    header.segment_size += 1U;
    EXPECT_EQ(mw::detail::validateSharedMemoryHeader(header, header.segment_size, 1024U),
              mw::detail::ShmValidation::InvalidSegmentSize);
    header.segment_size = mw::detail::kSharedMemoryHeaderSize + 1025U;
    header.payload_size = 1025U;
    EXPECT_EQ(mw::detail::validateSharedMemoryHeader(header, header.segment_size, 1024U),
              mw::detail::ShmValidation::PayloadTooLarge);
}

TEST(ShmProtocolTest, NotificationPreservesLocatorAndHasPayloadIndependentSize) {
    const auto small = mw::detail::encodeShmNotification(makeNotification(1024U));
    const auto large = mw::detail::encodeShmNotification(makeNotification(4U * 1024U * 1024U));
    ASSERT_TRUE(small.has_value());
    ASSERT_TRUE(large.has_value());
    EXPECT_EQ(small->size(), mw::detail::kShmNotificationSize);
    EXPECT_EQ(large->size(), mw::detail::kShmNotificationSize);
    EXPECT_LT(large->size(), 1024U);

    const auto decoded = mw::detail::decodeShmNotification(small->data(), small->size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->handle.shm_name, "/mw_shm_protocol_test");
    EXPECT_EQ(decoded->handle.sequence, 7U);
    EXPECT_EQ(decoded->handle.publish_timestamp_ns, 123456U);
    EXPECT_EQ(decoded->handle.topic_id, 99U);
    EXPECT_EQ(mw::detail::validateShmNotification(*decoded, 1024U, 99U),
              mw::detail::ShmValidation::Valid);
}

TEST(ShmProtocolTest, RejectsMalformedNotification) {
    auto notification = makeNotification();
    notification.magic = 0U;
    auto encoded = mw::detail::encodeShmNotification(notification);
    ASSERT_TRUE(encoded.has_value());
    auto decoded = mw::detail::decodeShmNotification(encoded->data(), encoded->size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(mw::detail::validateShmNotification(*decoded, 1024U, 99U),
              mw::detail::ShmValidation::BadMagic);

    notification = makeNotification();
    notification.version = 2U;
    encoded = mw::detail::encodeShmNotification(notification);
    decoded = mw::detail::decodeShmNotification(encoded->data(), encoded->size());
    EXPECT_EQ(mw::detail::validateShmNotification(*decoded, 1024U, 99U),
              mw::detail::ShmValidation::UnsupportedVersion);

    EXPECT_FALSE(mw::detail::decodeShmNotification(encoded->data(), encoded->size() - 1U));
    notification = makeNotification();
    notification.handle.shm_name = "/" + std::string(mw::detail::kMaxShmNameSize, 'x');
    EXPECT_FALSE(mw::detail::encodeShmNotification(notification));
    notification = makeNotification();
    notification.handle.segment_size++;
    encoded = mw::detail::encodeShmNotification(notification);
    decoded = mw::detail::decodeShmNotification(encoded->data(), encoded->size());
    EXPECT_EQ(mw::detail::validateShmNotification(*decoded, 1024U, 99U),
              mw::detail::ShmValidation::InvalidSegmentSize);
}

TEST(ShmProtocolTest, AckIsFixedMetadataAndValidatesSequence) {
    const mw::detail::ShmAck ack{mw::detail::kShmNotificationMagic,
                                 mw::detail::kShmNotificationVersion, mw::detail::ShmFrameKind::Ack,
                                 42U};
    const auto encoded = mw::detail::encodeShmAck(ack);
    EXPECT_EQ(encoded.size(), mw::detail::kShmAckSize);
    const auto decoded = mw::detail::decodeShmAck(encoded.data(), encoded.size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(mw::detail::validateShmAck(*decoded, 42U), mw::detail::ShmValidation::Valid);
    EXPECT_EQ(mw::detail::validateShmAck(*decoded, 43U),
              mw::detail::ShmValidation::MetadataMismatch);
}

} // namespace
