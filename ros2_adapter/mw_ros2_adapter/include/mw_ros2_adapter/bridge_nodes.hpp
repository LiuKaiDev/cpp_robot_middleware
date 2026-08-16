#pragma once

#include <mw/context.hpp>
#include <mw/publisher.hpp>
#include <mw/subscriber.hpp>

#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>

#include <memory>

#include "mw_ros2_adapter/bridge_config.hpp"
#include "mw_ros2_adapter/type_support.hpp"

namespace mw_ros2_adapter {

class Ros2ToMwBridge final : public rclcpp::Node {
  public:
    explicit Ros2ToMwBridge(const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});

  private:
    template <typename MessageT> void handleMessage(const MessageT& message);

    BridgeConfig config_;
    SupportedMessageType message_type_{SupportedMessageType::String};
    std::unique_ptr<mw::Context> context_;
    std::unique_ptr<mw::Publisher> publisher_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr string_subscription_;
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr twist_subscription_;
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_subscription_;
};

class MwToRos2Bridge final : public rclcpp::Node {
  public:
    explicit MwToRos2Bridge(const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});

  private:
    void pollMiddleware();
    bool hasRosSubscribers() const noexcept;
    void deserializeAndPublish(const void* data, std::size_t size);

    BridgeConfig config_;
    SupportedMessageType message_type_{SupportedMessageType::String};
    std::unique_ptr<mw::Context> context_;
    std::unique_ptr<mw::Subscriber> subscriber_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr string_publisher_;
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr twist_publisher_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_publisher_;
    rclcpp::TimerBase::SharedPtr poll_timer_;
};

} // namespace mw_ros2_adapter
