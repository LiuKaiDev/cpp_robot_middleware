#include "benchmark_common.hpp"

#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/sample_view.hpp>
#include <mw/subscriber.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <thread>

namespace {

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
        "--overflow-policy must be drop_newest, drop_oldest, or block_with_timeout");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const mw_benchmark::Arguments arguments{argc, argv};
        const mw_benchmark::CommonOptions options =
            mw_benchmark::parseCommonOptions(arguments, true);
        if (options.transport == mw_benchmark::TransportMode::Ros2 ||
            options.registry_path.empty() || options.socket_path.empty()) {
            throw std::invalid_argument(
                "custom subscriber requires custom transport, registry, and socket");
        }

        mw::Context context{"bench_subscriber_" + std::to_string(options.subscriber_index),
                            mw::RegistryConfig{options.registry_path}};
        mw::SubscriberConfig config;
        config.socket_path = options.socket_path;
        config.max_message_size = options.message_size;
        config.type_name = "mw.benchmark.Bytes";
        config.type_hash = "mw.benchmark.Bytes.v1";
        config.transport = options.transport == mw_benchmark::TransportMode::Uds
                               ? mw::TransportType::UnixDomainSocket
                               : mw::TransportType::SharedMemory;
        config.queue_depth = options.queue_depth;
        config.overflow_policy = parseOverflowPolicy(options.overflow_policy);
        config.block_timeout = options.block_timeout;
        auto subscriber = context.createSubscriber(options.topic, config);

        mw_benchmark::SubscriberAccumulator accumulator{
            options.message_size, options.latency_sample_stride, options.max_latency_samples};
        mw_benchmark::touchFile(mw_benchmark::subscriberReadyPath(options));

        std::uint32_t idle_after_stop = 0U;
        while (idle_after_stop < 3U) {
            bool received = false;
            if (options.transport == mw_benchmark::TransportMode::Uds) {
                auto message = subscriber.waitAndTake(options.receive_timeout);
                const std::uint64_t receive_ns = mw_benchmark::monotonicNowNs();
                if (message.has_value()) {
                    accumulator.observe(message->payload.data(), message->payload.size(), receive_ns);
                    received = true;
                }
            } else {
                auto view = subscriber.waitAndTakeView(options.receive_timeout);
                const std::uint64_t receive_ns = mw_benchmark::monotonicNowNs();
                if (view.has_value()) {
                    accumulator.observe(view->data(), view->size(), receive_ns);
                    received = true;
                }
            }
            if (received && options.slow_subscriber_us != 0U) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds{options.slow_subscriber_us});
            }

            std::error_code error;
            const bool stopping =
                std::filesystem::exists(mw_benchmark::markerPath(options, "stop"), error) && !error;
            if (stopping) {
                idle_after_stop = received ? 0U : idle_after_stop + 1U;
            }
        }

        mw_benchmark::writeSubscriberArtifacts(options, accumulator.measurements(),
                                               accumulator.totalPayloadErrors());
        return accumulator.totalPayloadErrors() == 0U ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "benchmark subscriber error: " << error.what() << '\n';
        return 2;
    }
}
