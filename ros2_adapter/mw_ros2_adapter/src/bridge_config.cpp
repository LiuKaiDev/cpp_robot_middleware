#include "mw_ros2_adapter/bridge_config.hpp"

#include "mw_ros2_adapter/type_support.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace mw_ros2_adapter {
namespace {

mw::TransportType parseTransport(const std::string& value) {
    if (value == "shm") {
        return mw::TransportType::SharedMemory;
    }
    if (value == "uds") {
        return mw::TransportType::UnixDomainSocket;
    }
    throw std::invalid_argument("transport must be 'shm' or 'uds'");
}

mw::OverflowPolicy parseOverflowPolicy(const std::string& value) {
    if (value == "drop_newest") {
        return mw::OverflowPolicy::DropNewest;
    }
    if (value == "drop_oldest") {
        return mw::OverflowPolicy::DropOldest;
    }
    if (value == "block_with_timeout") {
        return mw::OverflowPolicy::BlockWithTimeout;
    }
    throw std::invalid_argument(
        "overflow_policy must be drop_newest, drop_oldest, or block_with_timeout");
}

std::string defaultSocketPath(BridgeDirection direction) {
    const char* suffix = direction == BridgeDirection::MiddlewareToRos2 ? "mw2ros" : "ros2mw";
    return "/tmp/mw_ros2_" + std::to_string(::getpid()) + "_" + suffix + ".sock";
}

std::uint32_t checkedUint32(std::int64_t value, const char* name, std::uint32_t maximum) {
    if (value <= 0 || static_cast<std::uint64_t>(value) > maximum) {
        throw std::invalid_argument(std::string{name} + " is outside the supported range");
    }
    return static_cast<std::uint32_t>(value);
}

void validateText(const std::string& value, const char* name) {
    if (value.empty()) {
        throw std::invalid_argument(std::string{name} + " must not be empty");
    }
}

} // namespace

BridgeConfig declareAndLoadBridgeConfig(rclcpp::Node& node, BridgeDirection direction) {
    BridgeConfig config;
    config.registry_socket =
        node.declare_parameter<std::string>("registry_socket", "/tmp/mw_registry.sock");
    config.ros_topic = node.declare_parameter<std::string>("ros_topic", "/mw_bridge/ros_data");
    config.mw_topic = node.declare_parameter<std::string>("mw_topic", "/mw_bridge/data");
    config.message_type =
        node.declare_parameter<std::string>("message_type", "std_msgs/msg/String");
    config.mw_node_name = node.declare_parameter<std::string>(
        "mw_node_name", std::string{node.get_name()} + "_mw_" + std::to_string(::getpid()));
    config.mw_socket_path =
        node.declare_parameter<std::string>("mw_socket_path", defaultSocketPath(direction));
    config.transport = parseTransport(node.declare_parameter<std::string>("transport", "shm"));

    const std::int64_t max_message_size = node.declare_parameter<std::int64_t>(
        "max_message_size", static_cast<std::int64_t>(mw::kDefaultMaxMessageSize));
    if (max_message_size <= 0 ||
        static_cast<std::uint64_t>(max_message_size) >
            static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument("max_message_size is outside the middleware protocol range");
    }
    config.max_message_size = static_cast<std::size_t>(max_message_size);

    config.ros_qos_depth = checkedUint32(node.declare_parameter<std::int64_t>("ros_qos_depth", 10),
                                         "ros_qos_depth", 65536U);
    config.queue_depth = checkedUint32(node.declare_parameter<std::int64_t>("queue_depth", 8),
                                       "queue_depth", 65536U);
    config.overflow_policy =
        parseOverflowPolicy(node.declare_parameter<std::string>("overflow_policy", "drop_oldest"));
    config.block_timeout = std::chrono::milliseconds{checkedUint32(
        node.declare_parameter<std::int64_t>("block_timeout_ms", 100), "block_timeout_ms",
        static_cast<std::uint32_t>(std::numeric_limits<int>::max()))};
    config.poll_period = std::chrono::milliseconds{checkedUint32(
        node.declare_parameter<std::int64_t>("poll_period_ms", 2), "poll_period_ms", 60000U)};
    config.max_samples_per_poll =
        checkedUint32(node.declare_parameter<std::int64_t>("max_samples_per_poll", 16),
                      "max_samples_per_poll", 65536U);

    validateText(config.registry_socket, "registry_socket");
    validateText(config.ros_topic, "ros_topic");
    validateText(config.mw_topic, "mw_topic");
    validateText(config.message_type, "message_type");
    validateText(config.mw_node_name, "mw_node_name");
    if (!parseMessageType(config.message_type).has_value()) {
        throw std::invalid_argument(
            "unsupported message_type '" + config.message_type +
            "'; supported values are std_msgs/msg/String, geometry_msgs/msg/Twist, and "
            "sensor_msgs/msg/Image");
    }
    if (direction == BridgeDirection::MiddlewareToRos2) {
        validateText(config.mw_socket_path, "mw_socket_path");
    }
    return config;
}

mw::PublisherConfig makePublisherConfig(const BridgeConfig& config) {
    const SupportedMessageType type = parseMessageType(config.message_type).value();
    mw::PublisherConfig publisher;
    publisher.max_message_size = config.max_message_size;
    publisher.type_name = canonicalTypeName(type);
    publisher.type_hash = adapterWireTypeHash(type);
    publisher.transport = config.transport;
    return publisher;
}

mw::SubscriberConfig makeSubscriberConfig(const BridgeConfig& config) {
    const SupportedMessageType type = parseMessageType(config.message_type).value();
    mw::SubscriberConfig subscriber;
    subscriber.socket_path = config.mw_socket_path;
    subscriber.max_message_size = config.max_message_size;
    subscriber.type_name = canonicalTypeName(type);
    subscriber.type_hash = adapterWireTypeHash(type);
    subscriber.transport = config.transport;
    subscriber.queue_depth = config.queue_depth;
    subscriber.overflow_policy = config.overflow_policy;
    subscriber.block_timeout = config.block_timeout;
    return subscriber;
}

} // namespace mw_ros2_adapter
