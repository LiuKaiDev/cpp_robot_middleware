#include "mw_ros2_adapter/message_codec.hpp"
#include "mw_ros2_adapter/type_support.hpp"

#include "test_messages.hpp"

#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/loaned_sample.hpp>
#include <mw/result.hpp>

#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct Options {
    std::string role;
    std::string registry;
    std::string ros_topic;
    std::string mw_topic;
    std::string message_type{"std_msgs/msg/String"};
    std::string socket_path;
    std::string ready_file;
    std::string text{"adapter-string-payload"};
    std::string transport{"shm"};
    std::chrono::milliseconds timeout{15000};
    bool corrupt{false};
    bool expect_type_mismatch{false};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view name{argv[index]};
        if (name == "--corrupt") {
            options.corrupt = true;
            continue;
        }
        if (name == "--expect-type-mismatch") {
            options.expect_type_mismatch = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string{name});
        }
        const std::string value{argv[++index]};
        if (name == "--role") {
            options.role = value;
        } else if (name == "--registry") {
            options.registry = value;
        } else if (name == "--ros-topic") {
            options.ros_topic = value;
        } else if (name == "--mw-topic") {
            options.mw_topic = value;
        } else if (name == "--message-type") {
            options.message_type = value;
        } else if (name == "--socket") {
            options.socket_path = value;
        } else if (name == "--ready-file") {
            options.ready_file = value;
        } else if (name == "--text") {
            options.text = value;
        } else if (name == "--transport") {
            options.transport = value;
        } else if (name == "--timeout-ms") {
            options.timeout = std::chrono::milliseconds{std::stoll(value)};
        } else {
            throw std::invalid_argument("unknown option " + std::string{name});
        }
    }
    if (options.role.empty()) {
        throw std::invalid_argument("--role is required");
    }
    return options;
}

mw::TransportType transport(const Options& options) {
    if (options.transport == "shm") {
        return mw::TransportType::SharedMemory;
    }
    if (options.transport == "uds") {
        return mw::TransportType::UnixDomainSocket;
    }
    throw std::invalid_argument("unsupported transport");
}

template <typename MessageT> MessageT expectedMessage(const Options& options);

template <> std_msgs::msg::String expectedMessage(const Options& options) {
    return mw_ros2_adapter::test::makeString(options.text);
}

template <> geometry_msgs::msg::Twist expectedMessage(const Options&) {
    return mw_ros2_adapter::test::makeTwist();
}

template <> sensor_msgs::msg::Image expectedMessage(const Options&) {
    return mw_ros2_adapter::test::makeImage();
}

std::string processName(const char* prefix) {
    return std::string{prefix} + "_" + std::to_string(::getpid());
}

template <typename MessageT> int runRosPublisher(const Options& options) {
    auto node = std::make_shared<rclcpp::Node>(processName("adapter_ros_publisher"));
    auto publisher =
        node->create_publisher<MessageT>(options.ros_topic, rclcpp::QoS(10).reliable());
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    while (publisher->get_subscription_count() == 0U) {
        if (std::chrono::steady_clock::now() >= deadline) {
            std::cerr << "ROS publisher timed out waiting for bridge subscription\n";
            return 3;
        }
        rclcpp::spin_some(node);
        std::this_thread::sleep_for(5ms);
    }

    publisher->publish(expectedMessage<MessageT>(options));
    if (!publisher->wait_for_all_acked(options.timeout)) {
        std::cerr << "ROS publisher timed out waiting for acknowledgements\n";
        return 4;
    }
    return 0;
}

template <typename MessageT> int runRosSubscriber(const Options& options) {
    auto node = std::make_shared<rclcpp::Node>(processName("adapter_ros_subscriber"));
    bool received = false;
    bool valid = false;
    const MessageT expected = expectedMessage<MessageT>(options);
    auto subscription = node->create_subscription<MessageT>(
        options.ros_topic, rclcpp::QoS(10).reliable(),
        [&](typename MessageT::ConstSharedPtr message) {
            received = true;
            valid = mw_ros2_adapter::test::equal(expected, *message);
        });

    bool ready_reported = options.ready_file.empty();
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    while (!received && std::chrono::steady_clock::now() < deadline) {
        rclcpp::spin_some(node);
        if (!ready_reported && subscription->get_publisher_count() > 0U) {
            std::ofstream ready{options.ready_file};
            ready << "ready\n";
            ready_reported = true;
        }
        std::this_thread::sleep_for(2ms);
    }
    if (!received) {
        std::cerr << "ROS subscriber timed out waiting for bridge publication\n";
        return 3;
    }
    if (!valid) {
        std::cerr << "ROS subscriber received incorrect message\n";
        return 4;
    }
    return 0;
}

template <typename MessageT> std::vector<std::uint8_t> serializedMessage(const Options& options) {
    if (options.corrupt) {
        return {0x00U, 0x01U, 0x02U, 0x03U};
    }
    return mw_ros2_adapter::MessageCodec::serialize(expectedMessage<MessageT>(options));
}

mw::PublisherConfig publisherConfig(const Options& options) {
    const auto type = mw_ros2_adapter::parseMessageType(options.message_type);
    if (!type.has_value()) {
        throw std::invalid_argument("unsupported test message type");
    }
    mw::PublisherConfig config;
    config.max_message_size = mw::kDefaultMaxMessageSize;
    config.type_name = mw_ros2_adapter::canonicalTypeName(*type);
    config.type_hash = mw_ros2_adapter::adapterWireTypeHash(*type);
    config.transport = transport(options);
    return config;
}

mw::SubscriberConfig subscriberConfig(const Options& options) {
    const auto type = mw_ros2_adapter::parseMessageType(options.message_type);
    if (!type.has_value()) {
        throw std::invalid_argument("unsupported test message type");
    }
    mw::SubscriberConfig config;
    config.socket_path = options.socket_path;
    config.max_message_size = mw::kDefaultMaxMessageSize;
    config.type_name = mw_ros2_adapter::canonicalTypeName(*type);
    config.type_hash = mw_ros2_adapter::adapterWireTypeHash(*type);
    config.transport = transport(options);
    config.queue_depth = 4U;
    config.overflow_policy = mw::OverflowPolicy::DropOldest;
    return config;
}

template <typename MessageT> int runMiddlewarePublisher(const Options& options) {
    try {
        mw::Context context{processName("adapter_mw_publisher"),
                            mw::RegistryConfig{options.registry}};
        auto publisher = context.createPublisher(options.mw_topic, publisherConfig(options));
        const std::vector<std::uint8_t> bytes = serializedMessage<MessageT>(options);
        const mw::PublishResult result = publisher.publish(bytes.data(), bytes.size());
        if (!result) {
            std::cerr << "middleware publish failed: " << mw::errorMessage(result.error) << '\n';
            return 4;
        }
        std::cout << "middleware_payload_size=" << bytes.size() << '\n';
        return options.expect_type_mismatch ? 5 : 0;
    } catch (const mw::MiddlewareError& error) {
        if (options.expect_type_mismatch && error.code() == mw::ErrorCode::TypeMismatch) {
            return 0;
        }
        std::cerr << "middleware error: " << error.what() << '\n';
        return 3;
    }
}

template <typename MessageT> int runMiddlewareSubscriber(const Options& options) {
    mw::Context context{processName("adapter_mw_subscriber"), mw::RegistryConfig{options.registry}};
    auto subscriber = context.createSubscriber(options.mw_topic, subscriberConfig(options));
    std::vector<std::uint8_t> bytes;
    if (transport(options) == mw::TransportType::SharedMemory) {
        auto view = subscriber.waitAndTakeView(options.timeout);
        if (!view.has_value()) {
            std::cerr << "middleware receive failed: " << mw::errorMessage(subscriber.lastError())
                      << '\n';
            return 3;
        }
        const auto* begin = static_cast<const std::uint8_t*>(view->data());
        bytes.assign(begin, begin + view->size());
    } else {
        auto message = subscriber.waitAndTake(options.timeout);
        if (!message.has_value()) {
            std::cerr << "middleware receive failed: " << mw::errorMessage(subscriber.lastError())
                      << '\n';
            return 3;
        }
        bytes = std::move(message->payload);
    }

    try {
        const MessageT actual = mw_ros2_adapter::MessageCodec::deserialize<MessageT>(bytes);
        if (!mw_ros2_adapter::test::equal(expectedMessage<MessageT>(options), actual)) {
            std::cerr << "middleware subscriber decoded incorrect message\n";
            return 4;
        }
    } catch (const std::exception& error) {
        std::cerr << "middleware subscriber decode failed: " << error.what() << '\n';
        return 5;
    }
    std::cout << "middleware_payload_size=" << bytes.size() << '\n';
    return 0;
}

template <typename Function> int dispatchType(const Options& options, Function&& function) {
    const auto type = mw_ros2_adapter::parseMessageType(options.message_type);
    if (!type.has_value()) {
        throw std::invalid_argument("unsupported test message type");
    }
    switch (*type) {
    case mw_ros2_adapter::SupportedMessageType::String:
        return function.template operator()<std_msgs::msg::String>();
    case mw_ros2_adapter::SupportedMessageType::Twist:
        return function.template operator()<geometry_msgs::msg::Twist>();
    case mw_ros2_adapter::SupportedMessageType::Image:
        return function.template operator()<sensor_msgs::msg::Image>();
    }
    return 2;
}

struct RosPublisherRunner {
    const Options& options;
    template <typename MessageT> int operator()() const {
        return runRosPublisher<MessageT>(options);
    }
};

struct RosSubscriberRunner {
    const Options& options;
    template <typename MessageT> int operator()() const {
        return runRosSubscriber<MessageT>(options);
    }
};

struct MiddlewarePublisherRunner {
    const Options& options;
    template <typename MessageT> int operator()() const {
        return runMiddlewarePublisher<MessageT>(options);
    }
};

struct MiddlewareSubscriberRunner {
    const Options& options;
    template <typename MessageT> int operator()() const {
        return runMiddlewareSubscriber<MessageT>(options);
    }
};

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        if (options.role == "print-image-size") {
            const auto bytes =
                mw_ros2_adapter::MessageCodec::serialize(mw_ros2_adapter::test::makeImage());
            std::cout << "raw_image_size=" << mw_ros2_adapter::test::makeImage().data.size()
                      << " serialized_size=" << bytes.size() << '\n';
            return 0;
        }

        const bool ros_role = options.role == "ros-publisher" || options.role == "ros-subscriber";
        if (ros_role) {
            rclcpp::init(argc, argv);
        }

        int result = 2;
        if (options.role == "ros-publisher") {
            result = dispatchType(options, RosPublisherRunner{options});
        } else if (options.role == "ros-subscriber") {
            result = dispatchType(options, RosSubscriberRunner{options});
        } else if (options.role == "mw-publisher") {
            result = dispatchType(options, MiddlewarePublisherRunner{options});
        } else if (options.role == "mw-subscriber") {
            result = dispatchType(options, MiddlewareSubscriberRunner{options});
        } else {
            throw std::invalid_argument("unsupported --role");
        }

        if (ros_role) {
            rclcpp::shutdown();
        }
        return result;
    } catch (const std::exception& error) {
        std::cerr << "test peer: " << error.what() << '\n';
        if (rclcpp::ok()) {
            rclcpp::shutdown();
        }
        return 2;
    }
}
