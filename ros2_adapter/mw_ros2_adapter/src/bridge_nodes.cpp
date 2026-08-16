#include "mw_ros2_adapter/bridge_nodes.hpp"

#include "mw_ros2_adapter/message_codec.hpp"

#include <mw/config.hpp>
#include <mw/loaned_sample.hpp>
#include <mw/result.hpp>
#include <mw/sample_view.hpp>

#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mw_ros2_adapter {
namespace {

rclcpp::QoS bridgeQos(std::uint32_t depth) {
    return rclcpp::QoS(rclcpp::KeepLast(depth)).reliable();
}

bool isNoData(mw::ErrorCode error) noexcept {
    return error == mw::ErrorCode::Timeout || error == mw::ErrorCode::Ok;
}

} // namespace

Ros2ToMwBridge::Ros2ToMwBridge(const rclcpp::NodeOptions& options)
    : rclcpp::Node("ros2_to_mw_bridge", options),
      config_(declareAndLoadBridgeConfig(*this, BridgeDirection::Ros2ToMiddleware)),
      message_type_(parseMessageType(config_.message_type).value()) {
    context_ = std::make_unique<mw::Context>(config_.mw_node_name,
                                             mw::RegistryConfig{config_.registry_socket});
    publisher_ = std::make_unique<mw::Publisher>(
        context_->createPublisher(config_.mw_topic, makePublisherConfig(config_)));

    const rclcpp::QoS qos = bridgeQos(config_.ros_qos_depth);
    switch (message_type_) {
    case SupportedMessageType::String:
        string_subscription_ = create_subscription<std_msgs::msg::String>(
            config_.ros_topic, qos,
            [this](std_msgs::msg::String::ConstSharedPtr message) { handleMessage(*message); });
        break;
    case SupportedMessageType::Twist:
        twist_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
            config_.ros_topic, qos,
            [this](geometry_msgs::msg::Twist::ConstSharedPtr message) { handleMessage(*message); });
        break;
    case SupportedMessageType::Image:
        image_subscription_ = create_subscription<sensor_msgs::msg::Image>(
            config_.ros_topic, qos,
            [this](sensor_msgs::msg::Image::ConstSharedPtr message) { handleMessage(*message); });
        break;
    }

    RCLCPP_INFO(get_logger(), "bridging ROS '%s' -> middleware '%s' as %s over %s",
                config_.ros_topic.c_str(), config_.mw_topic.c_str(), config_.message_type.c_str(),
                mw::transportTypeName(config_.transport));
}

template <typename MessageT> void Ros2ToMwBridge::handleMessage(const MessageT& message) {
    try {
        const std::vector<std::uint8_t> serialized = MessageCodec::serialize(message);
        if (serialized.size() > config_.max_message_size) {
            RCLCPP_ERROR(get_logger(), "serialized ROS payload is %zu bytes, limit is %zu bytes",
                         serialized.size(), config_.max_message_size);
            return;
        }

        mw::PublishResult result;
        if (config_.transport == mw::TransportType::SharedMemory) {
            mw::LoanedSample loan = publisher_->loan(serialized.size());
            if (!loan) {
                RCLCPP_ERROR(get_logger(), "middleware loan failed: %s",
                             mw::errorMessage(loan.error()));
                return;
            }
            std::memcpy(loan.data(), serialized.data(), serialized.size());
            result = loan.publish();
        } else {
            result = publisher_->publish(serialized.data(), serialized.size());
        }

        if (!result) {
            RCLCPP_ERROR(get_logger(), "middleware publish failed: %s",
                         mw::errorMessage(result.error));
        }
    } catch (const std::exception& error) {
        RCLCPP_ERROR(get_logger(), "ROS serialization/publish failed: %s", error.what());
    }
}

MwToRos2Bridge::MwToRos2Bridge(const rclcpp::NodeOptions& options)
    : rclcpp::Node("mw_to_ros2_bridge", options),
      config_(declareAndLoadBridgeConfig(*this, BridgeDirection::MiddlewareToRos2)),
      message_type_(parseMessageType(config_.message_type).value()) {
    context_ = std::make_unique<mw::Context>(config_.mw_node_name,
                                             mw::RegistryConfig{config_.registry_socket});
    subscriber_ = std::make_unique<mw::Subscriber>(
        context_->createSubscriber(config_.mw_topic, makeSubscriberConfig(config_)));

    const rclcpp::QoS qos = bridgeQos(config_.ros_qos_depth);
    switch (message_type_) {
    case SupportedMessageType::String:
        string_publisher_ = create_publisher<std_msgs::msg::String>(config_.ros_topic, qos);
        break;
    case SupportedMessageType::Twist:
        twist_publisher_ = create_publisher<geometry_msgs::msg::Twist>(config_.ros_topic, qos);
        break;
    case SupportedMessageType::Image:
        image_publisher_ = create_publisher<sensor_msgs::msg::Image>(config_.ros_topic, qos);
        break;
    }

    poll_timer_ = create_wall_timer(config_.poll_period, [this]() { pollMiddleware(); });
    RCLCPP_INFO(get_logger(), "bridging middleware '%s' -> ROS '%s' as %s over %s",
                config_.mw_topic.c_str(), config_.ros_topic.c_str(), config_.message_type.c_str(),
                mw::transportTypeName(config_.transport));
}

void MwToRos2Bridge::pollMiddleware() {
    if (!hasRosSubscribers()) {
        return;
    }
    for (std::uint32_t index = 0; index < config_.max_samples_per_poll; ++index) {
        if (config_.transport == mw::TransportType::SharedMemory) {
            auto view = subscriber_->takeView();
            if (!view.has_value()) {
                if (!isNoData(subscriber_->lastError())) {
                    RCLCPP_WARN(get_logger(), "middleware receive failed: %s",
                                mw::errorMessage(subscriber_->lastError()));
                }
                break;
            }
            deserializeAndPublish(view->data(), view->size());
        } else {
            auto message = subscriber_->take();
            if (!message.has_value()) {
                if (!isNoData(subscriber_->lastError())) {
                    RCLCPP_WARN(get_logger(), "middleware receive failed: %s",
                                mw::errorMessage(subscriber_->lastError()));
                }
                break;
            }
            deserializeAndPublish(message->payload.data(), message->payload.size());
        }
    }
}

bool MwToRos2Bridge::hasRosSubscribers() const noexcept {
    switch (message_type_) {
    case SupportedMessageType::String:
        return string_publisher_->get_subscription_count() > 0U;
    case SupportedMessageType::Twist:
        return twist_publisher_->get_subscription_count() > 0U;
    case SupportedMessageType::Image:
        return image_publisher_->get_subscription_count() > 0U;
    }
    return false;
}

void MwToRos2Bridge::deserializeAndPublish(const void* data, std::size_t size) {
    try {
        switch (message_type_) {
        case SupportedMessageType::String:
            string_publisher_->publish(
                MessageCodec::deserialize<std_msgs::msg::String>(data, size));
            break;
        case SupportedMessageType::Twist:
            twist_publisher_->publish(
                MessageCodec::deserialize<geometry_msgs::msg::Twist>(data, size));
            break;
        case SupportedMessageType::Image:
            image_publisher_->publish(
                MessageCodec::deserialize<sensor_msgs::msg::Image>(data, size));
            break;
        }
    } catch (const std::exception& error) {
        RCLCPP_ERROR(get_logger(), "ROS deserialization failed: %s", error.what());
    }
}

} // namespace mw_ros2_adapter
