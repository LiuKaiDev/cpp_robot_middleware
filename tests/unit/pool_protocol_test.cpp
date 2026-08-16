#include "detail/pool_protocol.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using mw::detail::ChunkHandle;
using mw::detail::PoolDescriptor;
using mw::detail::PoolNotification;
using mw::detail::PoolRelease;

TEST(PoolProtocolTest, NotificationIsFixedMetadataAndRoundTripsLogicalChunk) {
    const PoolNotification notification{
        {"/mw_pool_protocol_codec", 81U, 4096U, 17U, mw::detail::kPoolLayoutVersion},
        {81U, 9U, 4U, 2048U},
        1000U,
        27U,
        123456U};
    const auto encoded = mw::detail::encodePoolNotification(notification);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(encoded->size(), mw::detail::kPoolNotificationSize);
    EXPECT_LT(encoded->size(), 1024U);

    const auto decoded = mw::detail::decodePoolNotification(encoded->data(), encoded->size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->pool.shm_name, notification.pool.shm_name);
    EXPECT_EQ(decoded->pool.pool_id, notification.pool.pool_id);
    EXPECT_EQ(decoded->pool.segment_size, notification.pool.segment_size);
    EXPECT_EQ(decoded->pool.topic_id, notification.pool.topic_id);
    EXPECT_EQ(decoded->handle, notification.handle);
    EXPECT_EQ(decoded->payload_size, notification.payload_size);
    EXPECT_EQ(decoded->sequence, notification.sequence);
    EXPECT_EQ(decoded->publish_timestamp_ns, notification.publish_timestamp_ns);
    EXPECT_EQ(mw::detail::validatePoolNotification(*decoded, 4096U, 17U), mw::ErrorCode::Ok);
}

TEST(PoolProtocolTest, RejectsMalformedNotificationAndBounds) {
    PoolNotification notification{{"bad/name", 1U, 4096U, 2U, mw::detail::kPoolLayoutVersion},
                                  {1U, 0U, 1U, 512U},
                                  64U,
                                  1U,
                                  2U};
    EXPECT_FALSE(mw::detail::encodePoolNotification(notification).has_value());

    notification.pool.shm_name = "/mw_pool_protocol_valid";
    const auto encoded = mw::detail::encodePoolNotification(notification);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_FALSE(mw::detail::decodePoolNotification(encoded->data(), encoded->size() - 1U));

    auto bad_padding = *encoded;
    bad_padding.back() = 1U;
    EXPECT_FALSE(mw::detail::decodePoolNotification(bad_padding.data(), bad_padding.size()));

    EXPECT_EQ(mw::detail::validatePoolNotification(notification, 32U, 2U),
              mw::ErrorCode::MessageTooLarge);
    notification.handle.generation = 0U;
    EXPECT_EQ(mw::detail::validatePoolNotification(notification, 4096U, 2U),
              mw::ErrorCode::InvalidFrame);
}

TEST(PoolProtocolTest, ReleaseRoundTripsAndRejectsStaleIdentity) {
    const ChunkHandle handle{99U, 7U, 3U, 8192U};
    const PoolRelease release{handle, 42U};
    const auto encoded = mw::detail::encodePoolRelease(release);
    EXPECT_EQ(encoded.size(), mw::detail::kPoolReleaseSize);
    const auto decoded = mw::detail::decodePoolRelease(encoded.data(), encoded.size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->handle, handle);
    EXPECT_EQ(decoded->sequence, 42U);
    EXPECT_EQ(mw::detail::validatePoolRelease(*decoded, handle, 42U), mw::ErrorCode::Ok);

    ChunkHandle stale = handle;
    ++stale.generation;
    EXPECT_EQ(mw::detail::validatePoolRelease(*decoded, stale, 42U),
              mw::ErrorCode::InvalidChunkHandle);
    EXPECT_FALSE(mw::detail::decodePoolRelease(encoded.data(), encoded.size() - 1U));
}

} // namespace
