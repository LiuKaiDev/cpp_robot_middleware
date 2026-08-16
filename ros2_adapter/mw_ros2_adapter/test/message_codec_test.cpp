#include "mw_ros2_adapter/message_codec.hpp"
#include "mw_ros2_adapter/type_support.hpp"

#include <mw/config.hpp>

#include <geometry_msgs/msg/twist.hpp>
#include <gtest/gtest.h>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace mw_ros2_adapter {
namespace {

sensor_msgs::msg::Image makeImage(std::uint32_t width, std::uint32_t height) {
    sensor_msgs::msg::Image image;
    image.header.stamp.sec = 123;
    image.header.stamp.nanosec = 456789U;
    image.header.frame_id = "phase7_camera";
    image.height = height;
    image.width = width;
    image.encoding = "rgb8";
    image.is_bigendian = 0U;
    image.step = width * 3U;
    image.data.resize(static_cast<std::size_t>(image.step) * height);
    for (std::size_t index = 0; index < image.data.size(); ++index) {
        image.data[index] = static_cast<std::uint8_t>((index * 37U + 11U) & 0xFFU);
    }
    return image;
}

void expectImageEqual(const sensor_msgs::msg::Image& expected,
                      const sensor_msgs::msg::Image& actual) {
    EXPECT_EQ(actual.header.stamp.sec, expected.header.stamp.sec);
    EXPECT_EQ(actual.header.stamp.nanosec, expected.header.stamp.nanosec);
    EXPECT_EQ(actual.header.frame_id, expected.header.frame_id);
    EXPECT_EQ(actual.height, expected.height);
    EXPECT_EQ(actual.width, expected.width);
    EXPECT_EQ(actual.encoding, expected.encoding);
    EXPECT_EQ(actual.is_bigendian, expected.is_bigendian);
    EXPECT_EQ(actual.step, expected.step);
    EXPECT_EQ(actual.data, expected.data);
}

TEST(MessageCodecTest, RoundTripsEmptySmallUtf8AndLongStrings) {
    const std::vector<std::string> values{
        "",
        "phase7",
        "UTF-8: \xE4\xBD\xA0\xE5\xA5\xBD",
        std::string(256U * 1024U, 'x'),
    };

    for (const std::string& value : values) {
        std_msgs::msg::String source;
        source.data = value;
        const auto serialized = MessageCodec::serialize(source);
        const auto decoded = MessageCodec::deserialize<std_msgs::msg::String>(serialized);
        EXPECT_EQ(decoded.data, value);
    }
}

TEST(MessageCodecTest, RoundTripsAllTwistFields) {
    geometry_msgs::msg::Twist source;
    source.linear.x = 1.25;
    source.linear.y = -2.5;
    source.linear.z = 0.0;
    source.angular.x = -0.125;
    source.angular.y = 9.75;
    source.angular.z = 3.141592653589793;

    const auto decoded =
        MessageCodec::deserialize<geometry_msgs::msg::Twist>(MessageCodec::serialize(source));
    EXPECT_DOUBLE_EQ(decoded.linear.x, source.linear.x);
    EXPECT_DOUBLE_EQ(decoded.linear.y, source.linear.y);
    EXPECT_DOUBLE_EQ(decoded.linear.z, source.linear.z);
    EXPECT_DOUBLE_EQ(decoded.angular.x, source.angular.x);
    EXPECT_DOUBLE_EQ(decoded.angular.y, source.angular.y);
    EXPECT_DOUBLE_EQ(decoded.angular.z, source.angular.z);
}

TEST(MessageCodecTest, RoundTripsImageMetadataAndBytes) {
    const sensor_msgs::msg::Image source = makeImage(320U, 240U);
    const auto decoded =
        MessageCodec::deserialize<sensor_msgs::msg::Image>(MessageCodec::serialize(source));
    expectImageEqual(source, decoded);
}

TEST(MessageCodecTest, LargeImageFitsMiddlewareFourMegabyteLimit) {
    const sensor_msgs::msg::Image source = makeImage(1280U, 720U);
    const auto serialized = MessageCodec::serialize(source);
    EXPECT_GT(serialized.size(), source.data.size());
    EXPECT_LT(serialized.size(), mw::kDefaultMaxMessageSize);
    expectImageEqual(source, MessageCodec::deserialize<sensor_msgs::msg::Image>(serialized));
}

TEST(MessageCodecTest, RejectsEmptyNullAndTruncatedPayloads) {
    EXPECT_THROW((MessageCodec::deserialize<std_msgs::msg::String>(nullptr, 0U)),
                 std::invalid_argument);
    EXPECT_THROW((MessageCodec::deserialize<std_msgs::msg::String>(nullptr, 8U)),
                 std::invalid_argument);

    sensor_msgs::msg::Image image = makeImage(64U, 32U);
    auto serialized = MessageCodec::serialize(image);
    serialized.resize(serialized.size() - 7U);
    EXPECT_ANY_THROW((MessageCodec::deserialize<sensor_msgs::msg::Image>(serialized)));
}

TEST(MessageCodecTest, RejectsSerializedPayloadForWrongRosType) {
    std_msgs::msg::String source;
    source.data = "not enough bytes to represent a Twist";
    const auto serialized = MessageCodec::serialize(source);
    EXPECT_ANY_THROW((MessageCodec::deserialize<geometry_msgs::msg::Twist>(serialized)));
}

TEST(TypeSupportTest, SupportsExactlyThePhase7Types) {
    EXPECT_EQ(parseMessageType("std_msgs/msg/String"), SupportedMessageType::String);
    EXPECT_EQ(parseMessageType("geometry_msgs/msg/Twist"), SupportedMessageType::Twist);
    EXPECT_EQ(parseMessageType("sensor_msgs/msg/Image"), SupportedMessageType::Image);
    EXPECT_FALSE(parseMessageType("sensor_msgs/msg/PointCloud2").has_value());
}

TEST(TypeSupportTest, WireIdentifiersAreStableVersionedAndDistinct) {
    const std::string string_hash = adapterWireTypeHash(SupportedMessageType::String);
    const std::string twist_hash = adapterWireTypeHash(SupportedMessageType::Twist);
    const std::string image_hash = adapterWireTypeHash(SupportedMessageType::Image);
    EXPECT_EQ(string_hash, "mw_ros2_adapter.cdr.v1.fnv1a64:1ff3a38aee1da4cf");
    EXPECT_EQ(twist_hash, "mw_ros2_adapter.cdr.v1.fnv1a64:59ab2544210b7af0");
    EXPECT_EQ(image_hash, "mw_ros2_adapter.cdr.v1.fnv1a64:e694a7eb7082215c");
}

} // namespace
} // namespace mw_ros2_adapter
