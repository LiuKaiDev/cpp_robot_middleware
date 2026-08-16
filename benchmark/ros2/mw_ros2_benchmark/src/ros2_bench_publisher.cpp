#include "benchmark_common.hpp"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <thread>
#include <unistd.h>

namespace {

using mw_benchmark::BenchmarkPhase;
using mw_benchmark::BenchmarkProfile;
using mw_benchmark::CommonOptions;
using mw_benchmark::PublisherMeasurements;

bool allSubscribersReady(const CommonOptions& options) {
    for (std::uint32_t index = 0U; index < options.subscriber_count; ++index) {
        CommonOptions subscriber = options;
        subscriber.subscriber_index = index;
        if (!mw_benchmark::waitForFile(mw_benchmark::subscriberReadyPath(subscriber),
                                       options.readiness_timeout)) {
            return false;
        }
    }
    return true;
}

class RosPublisherBenchmark {
  public:
    explicit RosPublisherBenchmark(CommonOptions options) : options_(std::move(options)) {
        node_ = std::make_shared<rclcpp::Node>("mw_ros2_bench_publisher_" +
                                              std::to_string(::getpid()));
        rclcpp::QoS qos{rclcpp::KeepLast(options_.queue_depth)};
        qos.reliable();
        publisher_ = node_->create_publisher<std_msgs::msg::UInt8MultiArray>(
            options_.topic, qos);
        message_.data.resize(options_.message_size);
    }

    bool waitForSubscribers() {
        const auto deadline = std::chrono::steady_clock::now() + options_.readiness_timeout;
        while (rclcpp::ok() && std::chrono::steady_clock::now() < deadline) {
            rclcpp::spin_some(node_);
            if (publisher_->get_subscription_count() >= options_.subscriber_count) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
        return false;
    }

    PublisherMeasurements runPhase(BenchmarkPhase phase, std::chrono::milliseconds duration) {
        PublisherMeasurements measurements;
        const auto deadline = std::chrono::steady_clock::now() + duration;
        const auto period = std::chrono::nanoseconds{
            static_cast<std::int64_t>(1000000000ULL / options_.publish_rate_hz)};
        auto next_publish = std::chrono::steady_clock::now();
        std::uint64_t sequence = 0U;
        while (rclcpp::ok() && std::chrono::steady_clock::now() < deadline) {
            if (options_.profile == BenchmarkProfile::Latency) {
                std::this_thread::sleep_until(next_publish);
                next_publish += period;
                if (std::chrono::steady_clock::now() >= deadline) {
                    break;
                }
            }
            ++measurements.publish_attempts;
            const std::uint64_t envelope_sequence =
                mw_benchmark::phaseSequenceBase(phase) + sequence + 1U;
            mw_benchmark::fillEnvelope(message_.data.data(), message_.data.size(),
                                       envelope_sequence);
            mw_benchmark::writePublishTimestamp(message_.data.data(), message_.data.size(),
                                                mw_benchmark::monotonicNowNs());
            publisher_->publish(message_);
            ++measurements.messages_published;
            ++sequence;
            if ((sequence & 0x3FFU) == 0U) {
                rclcpp::spin_some(node_);
            }
        }
        return measurements;
    }

  private:
    CommonOptions options_;
    std::shared_ptr<rclcpp::Node> node_;
    rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr publisher_;
    std_msgs::msg::UInt8MultiArray message_;
};

} // namespace

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    try {
        const mw_benchmark::Arguments arguments{argc, argv};
        const CommonOptions options = mw_benchmark::parseCommonOptions(arguments, false);
        if (options.transport != mw_benchmark::TransportMode::Ros2) {
            throw std::invalid_argument("ROS2 publisher requires --transport ros2");
        }
        if (!allSubscribersReady(options)) {
            throw std::runtime_error("not all ROS2 subscribers became locally ready");
        }
        RosPublisherBenchmark benchmark{options};
        if (!benchmark.waitForSubscribers()) {
            throw std::runtime_error("ROS2 subscription discovery timed out");
        }
        (void)benchmark.runPhase(BenchmarkPhase::Warmup, options.warmup);

        mw_benchmark::touchFile(mw_benchmark::markerPath(options, "measurement.start"));
        if (!mw_benchmark::waitForFile(
                mw_benchmark::markerPath(options, "measurement.start.ack"),
                options.readiness_timeout)) {
            throw std::runtime_error("monitor did not acknowledge measurement start");
        }
        const std::uint64_t start_ns = mw_benchmark::monotonicNowNs();
        const PublisherMeasurements measurements =
            benchmark.runPhase(BenchmarkPhase::Measurement, options.measurement);
        const std::uint64_t elapsed_ns = mw_benchmark::monotonicNowNs() - start_ns;
        mw_benchmark::touchFile(mw_benchmark::markerPath(options, "measurement.end"));
        if (!mw_benchmark::waitForFile(mw_benchmark::markerPath(options, "measurement.end.ack"),
                                       options.readiness_timeout)) {
            throw std::runtime_error("monitor did not acknowledge measurement end");
        }
        (void)benchmark.runPhase(BenchmarkPhase::Cooldown, options.cooldown);
        mw_benchmark::writePublisherArtifact(options, measurements, elapsed_ns);
        mw_benchmark::touchFile(mw_benchmark::markerPath(options, "stop"));
        rclcpp::shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ROS2 benchmark publisher error: " << error.what() << '\n';
        try {
            const mw_benchmark::Arguments arguments{argc, argv};
            const CommonOptions options = mw_benchmark::parseCommonOptions(arguments, false);
            mw_benchmark::touchFile(mw_benchmark::markerPath(options, "stop"));
        } catch (...) {
        }
        rclcpp::shutdown();
        return 2;
    }
}
