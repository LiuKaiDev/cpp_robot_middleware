#pragma once

#include <mw/config.hpp>

#include <rclcpp/node.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mw_ros2_adapter {

enum class BridgeDirection {
    Ros2ToMiddleware,
    MiddlewareToRos2,
};

struct BridgeConfig {
    std::string registry_socket;
    std::string ros_topic;
    std::string mw_topic;
    std::string message_type;
    std::string mw_node_name;
    std::string mw_socket_path;
    mw::TransportType transport{mw::TransportType::SharedMemory};
    std::size_t max_message_size{mw::kDefaultMaxMessageSize};
    std::uint32_t ros_qos_depth{10U};
    std::uint32_t queue_depth{8U};
    mw::OverflowPolicy overflow_policy{mw::OverflowPolicy::DropOldest};
    std::chrono::milliseconds block_timeout{100};
    std::chrono::milliseconds poll_period{2};
    std::uint32_t max_samples_per_poll{16U};
};

BridgeConfig declareAndLoadBridgeConfig(rclcpp::Node& node, BridgeDirection direction);
mw::PublisherConfig makePublisherConfig(const BridgeConfig& config);
mw::SubscriberConfig makeSubscriberConfig(const BridgeConfig& config);

} // namespace mw_ros2_adapter
