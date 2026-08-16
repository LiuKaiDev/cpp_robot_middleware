#pragma once

#include <geometry_msgs/msg/twist.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace mw_ros2_adapter::test {

inline std_msgs::msg::String makeString(const std::string& text) {
    std_msgs::msg::String message;
    message.data = text;
    return message;
}

inline geometry_msgs::msg::Twist makeTwist() {
    geometry_msgs::msg::Twist message;
    message.linear.x = 1.25;
    message.linear.y = -2.5;
    message.linear.z = 0.0;
    message.angular.x = -0.125;
    message.angular.y = 9.75;
    message.angular.z = 3.141592653589793;
    return message;
}

inline sensor_msgs::msg::Image makeImage() {
    sensor_msgs::msg::Image message;
    message.header.stamp.sec = 123;
    message.header.stamp.nanosec = 456789U;
    message.header.frame_id = "phase7_camera";
    message.height = 720U;
    message.width = 1280U;
    message.encoding = "rgb8";
    message.is_bigendian = 0U;
    message.step = message.width * 3U;
    message.data.resize(static_cast<std::size_t>(message.step) * message.height);
    for (std::size_t index = 0; index < message.data.size(); ++index) {
        message.data[index] = static_cast<std::uint8_t>((index * 37U + 11U) & 0xFFU);
    }
    return message;
}

inline bool equal(const std_msgs::msg::String& expected, const std_msgs::msg::String& actual) {
    return actual.data == expected.data;
}

inline bool equal(const geometry_msgs::msg::Twist& expected,
                  const geometry_msgs::msg::Twist& actual) {
    return actual.linear.x == expected.linear.x && actual.linear.y == expected.linear.y &&
           actual.linear.z == expected.linear.z && actual.angular.x == expected.angular.x &&
           actual.angular.y == expected.angular.y && actual.angular.z == expected.angular.z;
}

inline bool equal(const sensor_msgs::msg::Image& expected, const sensor_msgs::msg::Image& actual) {
    return actual.header.stamp.sec == expected.header.stamp.sec &&
           actual.header.stamp.nanosec == expected.header.stamp.nanosec &&
           actual.header.frame_id == expected.header.frame_id && actual.height == expected.height &&
           actual.width == expected.width && actual.encoding == expected.encoding &&
           actual.is_bigendian == expected.is_bigendian && actual.step == expected.step &&
           actual.data == expected.data;
}

} // namespace mw_ros2_adapter::test
