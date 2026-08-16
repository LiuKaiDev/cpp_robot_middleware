#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mw_benchmark {

inline constexpr std::size_t kEnvelopeHeaderSize = 16U;
inline constexpr std::uint64_t kWarmupSequenceBase = 0U;
inline constexpr std::uint64_t kMeasurementSequenceBase = 1ULL << 40U;
inline constexpr std::uint64_t kCooldownSequenceBase = 2ULL << 40U;

enum class BenchmarkPhase {
    Warmup,
    Measurement,
    Cooldown,
};

enum class BenchmarkProfile {
    Latency,
    Throughput,
};

enum class TransportMode {
    Uds,
    ShmCopy,
    ShmLoan,
    Ros2,
};

struct CommonOptions {
    TransportMode transport{TransportMode::Uds};
    BenchmarkProfile profile{BenchmarkProfile::Latency};
    std::size_t message_size{64U};
    std::uint32_t subscriber_count{1U};
    std::uint32_t subscriber_index{0U};
    std::uint32_t queue_depth{8U};
    std::uint64_t publish_rate_hz{1000U};
    std::uint64_t latency_sample_stride{1U};
    std::uint64_t max_latency_samples{1000000U};
    std::uint64_t slow_subscriber_us{0U};
    std::chrono::milliseconds warmup{2000};
    std::chrono::milliseconds measurement{5000};
    std::chrono::milliseconds cooldown{1000};
    std::chrono::milliseconds readiness_timeout{15000};
    std::chrono::milliseconds receive_timeout{100};
    std::chrono::milliseconds block_timeout{100};
    std::string overflow_policy{"block_with_timeout"};
    std::string registry_path;
    std::string socket_path;
    std::string topic;
    std::filesystem::path run_dir;
};

class Arguments {
  public:
    Arguments(int argc, char** argv);

    std::string require(std::string_view name) const;
    std::string get(std::string_view name, std::string default_value) const;
    std::uint64_t getUnsigned(std::string_view name, std::uint64_t default_value) const;
    bool contains(std::string_view name) const;
    void requireOnly(const std::vector<std::string>& allowed) const;

  private:
    std::map<std::string, std::string> values_;
};

CommonOptions parseCommonOptions(const Arguments& arguments, bool subscriber);

const char* transportName(TransportMode transport) noexcept;
const char* profileName(BenchmarkProfile profile) noexcept;
std::uint64_t phaseSequenceBase(BenchmarkPhase phase) noexcept;
BenchmarkPhase phaseFromSequence(std::uint64_t sequence) noexcept;

std::uint64_t monotonicNowNs() noexcept;
std::uint8_t deterministicPayloadByte(std::uint64_t sequence, std::size_t offset) noexcept;
void encodeU64(std::uint8_t* output, std::uint64_t value) noexcept;
std::uint64_t decodeU64(const std::uint8_t* input) noexcept;
void fillEnvelope(void* data, std::size_t size, std::uint64_t sequence,
                  std::uint64_t publish_timestamp_ns = 0U);
void writePublishTimestamp(void* data, std::size_t size, std::uint64_t timestamp_ns);

struct EnvelopeValidation {
    bool valid_size{false};
    bool valid_payload{false};
    std::uint64_t sequence{0U};
    std::uint64_t publish_timestamp_ns{0U};
};

EnvelopeValidation validateEnvelope(const void* data, std::size_t size,
                                    std::size_t expected_size) noexcept;

struct LatencyRecord {
    std::uint64_t sequence{0U};
    std::uint64_t latency_ns{0U};
};

struct SubscriberMeasurements {
    std::uint64_t messages_received{0U};
    std::uint64_t correct_messages_received{0U};
    std::uint64_t bytes_received{0U};
    std::uint64_t payload_errors{0U};
    std::uint64_t sequence_errors{0U};
    std::uint64_t sequence_gaps{0U};
    std::uint64_t duplicate_sequences{0U};
    std::uint64_t out_of_order_sequences{0U};
    std::uint64_t latency_samples_eligible{0U};
    std::uint64_t latency_samples_omitted{0U};
    std::vector<LatencyRecord> latencies;
};

class SubscriberAccumulator {
  public:
    SubscriberAccumulator(std::size_t expected_size, std::uint64_t sample_stride,
                          std::uint64_t max_samples);

    void observe(const void* data, std::size_t size, std::uint64_t receive_timestamp_ns) noexcept;
    const SubscriberMeasurements& measurements() const noexcept { return measurements_; }
    std::uint64_t totalPayloadErrors() const noexcept { return total_payload_errors_; }

  private:
    std::size_t expected_size_;
    std::uint64_t sample_stride_;
    std::uint64_t max_samples_;
    std::uint64_t last_measurement_sequence_{0U};
    bool have_measurement_sequence_{false};
    std::uint64_t total_payload_errors_{0U};
    SubscriberMeasurements measurements_;
};

struct PublisherMeasurements {
    std::uint64_t publish_attempts{0U};
    std::uint64_t messages_published{0U};
    std::uint64_t publish_errors{0U};
    std::uint64_t drop_count{0U};
    std::uint64_t queue_overflow_count{0U};
    std::uint64_t allocation_failure_count{0U};
    std::uint64_t blocked_count{0U};
    std::uint64_t blocked_time_ns{0U};
};

std::filesystem::path subscriberReadyPath(const CommonOptions& options);
std::filesystem::path subscriberSummaryPath(const CommonOptions& options);
std::filesystem::path subscriberLatencyPath(const CommonOptions& options);
std::filesystem::path markerPath(const CommonOptions& options, std::string_view marker);

void touchFile(const std::filesystem::path& path);
bool waitForFile(const std::filesystem::path& path, std::chrono::milliseconds timeout) noexcept;
void writeTextFile(const std::filesystem::path& path, const std::string& contents);
std::string jsonEscape(std::string_view value);
void writeSubscriberArtifacts(const CommonOptions& options,
                              const SubscriberMeasurements& measurements,
                              std::uint64_t total_payload_errors);
void writePublisherArtifact(const CommonOptions& options,
                            const PublisherMeasurements& measurements,
                            std::uint64_t measurement_elapsed_ns);

} // namespace mw_benchmark
