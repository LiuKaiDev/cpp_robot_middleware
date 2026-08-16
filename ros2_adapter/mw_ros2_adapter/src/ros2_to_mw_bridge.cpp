#include "mw_ros2_adapter/bridge_nodes.hpp"

#include <rclcpp/rclcpp.hpp>

#include <exception>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    try {
        auto node = std::make_shared<mw_ros2_adapter::Ros2ToMwBridge>();
        rclcpp::spin(node);
        node.reset();
        rclcpp::shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ros2_to_mw_bridge: " << error.what() << '\n';
        rclcpp::shutdown();
        return 2;
    }
}
