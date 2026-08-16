from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    arguments = [
        DeclareLaunchArgument(
            "direction",
            default_value="ros2_to_mw_bridge",
            description="ros2_to_mw_bridge or mw_to_ros2_bridge",
        ),
        DeclareLaunchArgument("node_name", default_value="mw_ros2_bridge"),
        DeclareLaunchArgument("registry_socket", default_value="/tmp/mw_registry.sock"),
        DeclareLaunchArgument("ros_topic", default_value="/mw_bridge/ros_data"),
        DeclareLaunchArgument("mw_topic", default_value="/mw_bridge/data"),
        DeclareLaunchArgument("message_type", default_value="std_msgs/msg/String"),
        DeclareLaunchArgument("transport", default_value="shm"),
        DeclareLaunchArgument("max_message_size", default_value="4194304"),
        DeclareLaunchArgument("queue_depth", default_value="8"),
        DeclareLaunchArgument("overflow_policy", default_value="drop_oldest"),
        DeclareLaunchArgument("block_timeout_ms", default_value="100"),
    ]

    bridge = Node(
        package="mw_ros2_adapter",
        executable=LaunchConfiguration("direction"),
        name=LaunchConfiguration("node_name"),
        output="screen",
        parameters=[
            {
                "registry_socket": LaunchConfiguration("registry_socket"),
                "ros_topic": LaunchConfiguration("ros_topic"),
                "mw_topic": LaunchConfiguration("mw_topic"),
                "message_type": LaunchConfiguration("message_type"),
                "transport": LaunchConfiguration("transport"),
                "max_message_size": ParameterValue(
                    LaunchConfiguration("max_message_size"), value_type=int
                ),
                "queue_depth": ParameterValue(
                    LaunchConfiguration("queue_depth"), value_type=int
                ),
                "overflow_policy": LaunchConfiguration("overflow_policy"),
                "block_timeout_ms": ParameterValue(
                    LaunchConfiguration("block_timeout_ms"), value_type=int
                ),
            }
        ],
    )
    return LaunchDescription(arguments + [bridge])
