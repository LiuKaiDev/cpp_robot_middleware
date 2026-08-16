#include "benchmark_common.hpp"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <thread>
#include <unistd.h>

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    try {
        const mw_benchmark::Arguments arguments{argc, argv};
        const mw_benchmark::CommonOptions options =
            mw_benchmark::parseCommonOptions(arguments, true);
        if (options.transport != mw_benchmark::TransportMode::Ros2) {
            throw std::invalid_argument("ROS2 subscriber requires --transport ros2");
        }

        auto node = std::make_shared<rclcpp::Node>(
            "mw_ros2_bench_subscriber_" + std::to_string(options.subscriber_index) + "_" +
            std::to_string(::getpid()));
        mw_benchmark::SubscriberAccumulator accumulator{
            options.message_size, options.latency_sample_stride, options.max_latency_samples};
        std::atomic<std::uint64_t> callback_count{0U};
        rclcpp::QoS qos{rclcpp::KeepLast(options.queue_depth)};
        qos.reliable();
        auto subscription = node->create_subscription<std_msgs::msg::UInt8MultiArray>(
            options.topic, qos,
            [&accumulator, &callback_count, &options](
                const std_msgs::msg::UInt8MultiArray::ConstSharedPtr message) {
                const std::uint64_t receive_ns = mw_benchmark::monotonicNowNs();
                accumulator.observe(message->data.data(), message->data.size(), receive_ns);
                callback_count.fetch_add(1U, std::memory_order_relaxed);
                if (options.slow_subscriber_us != 0U) {
                    std::this_thread::sleep_for(
                        std::chrono::microseconds{options.slow_subscriber_us});
                }
            });
        (void)subscription;
        mw_benchmark::touchFile(mw_benchmark::subscriberReadyPath(options));

        rclcpp::executors::SingleThreadedExecutor executor;
        executor.add_node(node);
        std::uint32_t idle_after_stop = 0U;
        std::uint64_t previous_count = 0U;
        while (rclcpp::ok() && idle_after_stop < 3U) {
            executor.spin_some(std::chrono::milliseconds{5});
            const std::uint64_t current_count = callback_count.load(std::memory_order_relaxed);
            std::error_code error;
            const bool stopping =
                std::filesystem::exists(mw_benchmark::markerPath(options, "stop"), error) && !error;
            if (stopping) {
                idle_after_stop = current_count == previous_count ? idle_after_stop + 1U : 0U;
            }
            previous_count = current_count;
            if (stopping) {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            } else {
                std::this_thread::yield();
            }
        }

        mw_benchmark::writeSubscriberArtifacts(options, accumulator.measurements(),
                                               accumulator.totalPayloadErrors());
        const bool valid = accumulator.totalPayloadErrors() == 0U;
        rclcpp::shutdown();
        return valid ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "ROS2 benchmark subscriber error: " << error.what() << '\n';
        rclcpp::shutdown();
        return 2;
    }
}
