#include "benchmark_common.hpp"

#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/loaned_sample.hpp>
#include <mw/publisher.hpp>
#include <mw/result.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using mw_benchmark::BenchmarkPhase;
using mw_benchmark::BenchmarkProfile;
using mw_benchmark::CommonOptions;
using mw_benchmark::PublisherMeasurements;
using mw_benchmark::TransportMode;

mw::MemoryPoolConfig benchmarkPool(std::uint32_t queue_depth) {
    mw::MemoryPoolConfig pool;
    const std::uint32_t minimum_count = std::max<std::uint32_t>(4U, queue_depth + 2U);
    for (mw::MemoryPoolClassConfig& size_class : pool.size_classes) {
        size_class.chunk_count = std::max(size_class.chunk_count, minimum_count);
    }
    return pool;
}

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

void accountResult(const mw::PublishResult& result, PublisherMeasurements& measurements) {
    measurements.drop_count += result.dropped_newest + result.dropped_oldest +
                               result.block_timeouts;
    measurements.queue_overflow_count += result.dropped_newest + result.dropped_oldest +
                                         result.block_timeouts;
    measurements.blocked_count += result.blocked_count;
    measurements.blocked_time_ns += result.blocked_time_ns;
    if (result.error == mw::ErrorCode::PoolExhausted) {
        ++measurements.allocation_failure_count;
    }
    if (result.enqueued > 0U) {
        ++measurements.messages_published;
    } else if (!result.ok()) {
        ++measurements.publish_errors;
    }
}

class CustomPublisherBenchmark {
  public:
    explicit CustomPublisherBenchmark(CommonOptions options)
        : options_(std::move(options)), context_("bench_publisher", mw::RegistryConfig{
                                                                        options_.registry_path}) {
        mw::PublisherConfig config;
        config.socket_path = options_.socket_path;
        config.max_message_size = options_.message_size;
        config.type_name = "mw.benchmark.Bytes";
        config.type_hash = "mw.benchmark.Bytes.v1";
        config.transport = options_.transport == TransportMode::Uds
                               ? mw::TransportType::UnixDomainSocket
                               : mw::TransportType::SharedMemory;
        config.memory_pool = benchmarkPool(options_.queue_depth);
        config.shm_ack_timeout = options_.readiness_timeout;
        publisher_.emplace(context_.createPublisher(options_.topic, config));
        copy_payload_.resize(options_.message_size);
    }

    PublisherMeasurements runPhase(BenchmarkPhase phase, std::chrono::milliseconds duration) {
        PublisherMeasurements measurements;
        const auto deadline = std::chrono::steady_clock::now() + duration;
        const auto period = std::chrono::nanoseconds{
            static_cast<std::int64_t>(1000000000ULL / options_.publish_rate_hz)};
        auto next_publish = std::chrono::steady_clock::now();
        std::uint64_t successful_sequence = 0U;

        while (std::chrono::steady_clock::now() < deadline) {
            if (options_.profile == BenchmarkProfile::Latency) {
                std::this_thread::sleep_until(next_publish);
                next_publish += period;
                if (std::chrono::steady_clock::now() >= deadline) {
                    break;
                }
            }
            ++measurements.publish_attempts;
            const std::uint64_t sequence =
                mw_benchmark::phaseSequenceBase(phase) + successful_sequence + 1U;
            mw::PublishResult result;
            if (options_.transport == TransportMode::ShmLoan) {
                mw::LoanedSample loan = publisher_->loan(options_.message_size);
                if (!loan) {
                    result.error = loan.error();
                    accountResult(result, measurements);
                    if (options_.profile == BenchmarkProfile::Throughput) {
                        std::this_thread::yield();
                    }
                    continue;
                }
                mw_benchmark::fillEnvelope(loan.data(), loan.size(), sequence);
                mw_benchmark::writePublishTimestamp(loan.data(), loan.size(),
                                                    mw_benchmark::monotonicNowNs());
                result = loan.publish();
            } else {
                mw_benchmark::fillEnvelope(copy_payload_.data(), copy_payload_.size(), sequence);
                mw_benchmark::writePublishTimestamp(copy_payload_.data(), copy_payload_.size(),
                                                    mw_benchmark::monotonicNowNs());
                result = publisher_->publish(copy_payload_.data(), copy_payload_.size());
            }
            accountResult(result, measurements);
            if (result.enqueued > 0U) {
                ++successful_sequence;
            } else if (options_.profile == BenchmarkProfile::Throughput) {
                std::this_thread::yield();
            }
        }
        return measurements;
    }

  private:
    CommonOptions options_;
    mw::Context context_;
    std::optional<mw::Publisher> publisher_;
    std::vector<std::uint8_t> copy_payload_;
};

} // namespace

int main(int argc, char** argv) {
    try {
        const mw_benchmark::Arguments arguments{argc, argv};
        CommonOptions options = mw_benchmark::parseCommonOptions(arguments, false);
        if (options.transport == TransportMode::Ros2 || options.registry_path.empty() ||
            options.socket_path.empty()) {
            throw std::invalid_argument("custom publisher requires custom transport, registry, and socket");
        }
        if (!allSubscribersReady(options)) {
            throw std::runtime_error("not all subscribers became ready");
        }

        CustomPublisherBenchmark benchmark{options};
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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "benchmark publisher error: " << error.what() << '\n';
        try {
            if (argc > 1) {
                const mw_benchmark::Arguments arguments{argc, argv};
                const CommonOptions options = mw_benchmark::parseCommonOptions(arguments, false);
                mw_benchmark::touchFile(mw_benchmark::markerPath(options, "stop"));
            }
        } catch (...) {
        }
        return 2;
    }
}
