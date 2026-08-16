#include "benchmark_common.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace mw_benchmark {
namespace {

std::uint64_t parseUnsigned(std::string_view text, std::string_view name) {
    std::uint64_t value = 0U;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid unsigned value for " + std::string(name));
    }
    return value;
}

std::uint32_t checkedU32(std::uint64_t value, std::string_view name) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string(name) + " exceeds uint32 range");
    }
    return static_cast<std::uint32_t>(value);
}

std::size_t checkedSize(std::uint64_t value, std::string_view name) {
    if (value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(std::string(name) + " exceeds size_t range");
    }
    return static_cast<std::size_t>(value);
}

std::chrono::milliseconds checkedMilliseconds(std::uint64_t value, std::string_view name) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument(std::string(name) + " exceeds duration range");
    }
    return std::chrono::milliseconds{static_cast<std::int64_t>(value)};
}

} // namespace

Arguments::Arguments(int argc, char** argv) {
    for (int index = 1; index < argc; index += 2) {
        const std::string name = argv[index];
        if (name.rfind("--", 0U) != 0U || index + 1 >= argc) {
            throw std::invalid_argument("arguments must be --name value pairs");
        }
        if (!values_.emplace(name, argv[index + 1]).second) {
            throw std::invalid_argument("duplicate argument " + name);
        }
    }
}

std::string Arguments::require(std::string_view name) const {
    const auto item = values_.find(std::string(name));
    if (item == values_.end() || item->second.empty()) {
        throw std::invalid_argument("missing required argument " + std::string(name));
    }
    return item->second;
}

std::string Arguments::get(std::string_view name, std::string default_value) const {
    const auto item = values_.find(std::string(name));
    return item == values_.end() ? std::move(default_value) : item->second;
}

std::uint64_t Arguments::getUnsigned(std::string_view name, std::uint64_t default_value) const {
    const auto item = values_.find(std::string(name));
    return item == values_.end() ? default_value : parseUnsigned(item->second, name);
}

bool Arguments::contains(std::string_view name) const {
    return values_.count(std::string(name)) != 0U;
}

void Arguments::requireOnly(const std::vector<std::string>& allowed) const {
    for (const auto& item : values_) {
        if (std::find(allowed.begin(), allowed.end(), item.first) == allowed.end()) {
            throw std::invalid_argument("unknown argument " + item.first);
        }
    }
}

CommonOptions parseCommonOptions(const Arguments& arguments, bool subscriber) {
    arguments.requireOnly({"--transport",          "--profile",
                           "--message-size",       "--subscribers",
                           "--subscriber-index",   "--queue-depth",
                           "--publish-rate-hz",    "--sample-stride",
                           "--max-samples",        "--slow-subscriber-us",
                           "--warmup-ms",          "--measurement-ms",
                           "--cooldown-ms",        "--readiness-timeout-ms",
                           "--receive-timeout-ms", "--block-timeout-ms",
                           "--overflow-policy",    "--registry",
                           "--socket",             "--topic",
                           "--run-dir"});

    CommonOptions options;
    const std::string transport = arguments.get("--transport", "uds");
    if (transport == "uds") {
        options.transport = TransportMode::Uds;
    } else if (transport == "shm_copy") {
        options.transport = TransportMode::ShmCopy;
    } else if (transport == "shm_loan") {
        options.transport = TransportMode::ShmLoan;
    } else if (transport == "ros2") {
        options.transport = TransportMode::Ros2;
    } else {
        throw std::invalid_argument("--transport must be uds, shm_copy, shm_loan, or ros2");
    }
    const std::string profile = arguments.get("--profile", "latency");
    if (profile == "latency") {
        options.profile = BenchmarkProfile::Latency;
    } else if (profile == "throughput") {
        options.profile = BenchmarkProfile::Throughput;
    } else {
        throw std::invalid_argument("--profile must be latency or throughput");
    }

    options.message_size = checkedSize(arguments.getUnsigned("--message-size", 64U),
                                       "--message-size");
    options.subscriber_count =
        checkedU32(arguments.getUnsigned("--subscribers", 1U), "--subscribers");
    options.subscriber_index =
        checkedU32(arguments.getUnsigned("--subscriber-index", 0U), "--subscriber-index");
    options.queue_depth =
        checkedU32(arguments.getUnsigned("--queue-depth", 8U), "--queue-depth");
    options.publish_rate_hz = arguments.getUnsigned("--publish-rate-hz", 1000U);
    options.latency_sample_stride = arguments.getUnsigned("--sample-stride", 1U);
    options.max_latency_samples = arguments.getUnsigned("--max-samples", 1000000U);
    options.slow_subscriber_us = arguments.getUnsigned("--slow-subscriber-us", 0U);
    options.warmup = checkedMilliseconds(arguments.getUnsigned("--warmup-ms", 2000U),
                                         "--warmup-ms");
    options.measurement = checkedMilliseconds(
        arguments.getUnsigned("--measurement-ms", 5000U), "--measurement-ms");
    options.cooldown = checkedMilliseconds(arguments.getUnsigned("--cooldown-ms", 1000U),
                                           "--cooldown-ms");
    options.readiness_timeout = checkedMilliseconds(
        arguments.getUnsigned("--readiness-timeout-ms", 15000U), "--readiness-timeout-ms");
    options.receive_timeout = checkedMilliseconds(
        arguments.getUnsigned("--receive-timeout-ms", 100U), "--receive-timeout-ms");
    options.block_timeout = checkedMilliseconds(
        arguments.getUnsigned("--block-timeout-ms", 100U), "--block-timeout-ms");
    options.overflow_policy = arguments.get("--overflow-policy", "block_with_timeout");
    options.registry_path = arguments.get("--registry", "");
    options.socket_path = arguments.get("--socket", "");
    options.topic = arguments.require("--topic");
    options.run_dir = arguments.require("--run-dir");

    if (options.message_size < kEnvelopeHeaderSize || options.subscriber_count == 0U ||
        options.queue_depth == 0U || options.publish_rate_hz == 0U ||
        options.latency_sample_stride == 0U || options.max_latency_samples == 0U ||
        options.measurement.count() <= 0 || options.readiness_timeout.count() <= 0 ||
        options.receive_timeout.count() <= 0 || options.block_timeout.count() <= 0) {
        throw std::invalid_argument("benchmark sizes, counts, rates, and timeouts must be positive");
    }
    if (subscriber && options.subscriber_index >= options.subscriber_count) {
        throw std::invalid_argument("subscriber index is outside topology");
    }
    return options;
}

const char* transportName(TransportMode transport) noexcept {
    switch (transport) {
    case TransportMode::Uds:
        return "uds";
    case TransportMode::ShmCopy:
        return "shm_copy";
    case TransportMode::ShmLoan:
        return "shm_loan";
    case TransportMode::Ros2:
        return "ros2";
    }
    return "unknown";
}

const char* profileName(BenchmarkProfile profile) noexcept {
    return profile == BenchmarkProfile::Latency ? "latency" : "throughput";
}

std::uint64_t phaseSequenceBase(BenchmarkPhase phase) noexcept {
    switch (phase) {
    case BenchmarkPhase::Warmup:
        return kWarmupSequenceBase;
    case BenchmarkPhase::Measurement:
        return kMeasurementSequenceBase;
    case BenchmarkPhase::Cooldown:
        return kCooldownSequenceBase;
    }
    return 0U;
}

BenchmarkPhase phaseFromSequence(std::uint64_t sequence) noexcept {
    if (sequence >= kCooldownSequenceBase) {
        return BenchmarkPhase::Cooldown;
    }
    if (sequence >= kMeasurementSequenceBase) {
        return BenchmarkPhase::Measurement;
    }
    return BenchmarkPhase::Warmup;
}

std::uint64_t monotonicNowNs() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::uint8_t deterministicPayloadByte(std::uint64_t sequence, std::size_t offset) noexcept {
    return static_cast<std::uint8_t>((sequence + (offset * 31U)) & 0xFFU);
}

void encodeU64(std::uint8_t* output, std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < 8U; ++index) {
        output[index] = static_cast<std::uint8_t>(value >> ((7U - index) * 8U));
    }
}

std::uint64_t decodeU64(const std::uint8_t* input) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value = (value << 8U) | input[index];
    }
    return value;
}

void fillEnvelope(void* data, std::size_t size, std::uint64_t sequence,
                  std::uint64_t publish_timestamp_ns) {
    if (data == nullptr || size < kEnvelopeHeaderSize) {
        throw std::invalid_argument("benchmark envelope is smaller than 16 bytes");
    }
    auto* bytes = static_cast<std::uint8_t*>(data);
    encodeU64(bytes, sequence);
    encodeU64(bytes + 8U, publish_timestamp_ns);
    for (std::size_t offset = kEnvelopeHeaderSize; offset < size; ++offset) {
        bytes[offset] = deterministicPayloadByte(sequence, offset);
    }
}

void writePublishTimestamp(void* data, std::size_t size, std::uint64_t timestamp_ns) {
    if (data == nullptr || size < kEnvelopeHeaderSize) {
        throw std::invalid_argument("benchmark envelope is smaller than 16 bytes");
    }
    encodeU64(static_cast<std::uint8_t*>(data) + 8U, timestamp_ns);
}

EnvelopeValidation validateEnvelope(const void* data, std::size_t size,
                                    std::size_t expected_size) noexcept {
    EnvelopeValidation result;
    result.valid_size = data != nullptr && size == expected_size && size >= kEnvelopeHeaderSize;
    if (!result.valid_size) {
        return result;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    result.sequence = decodeU64(bytes);
    result.publish_timestamp_ns = decodeU64(bytes + 8U);
    result.valid_payload = result.publish_timestamp_ns != 0U;
    for (std::size_t offset = kEnvelopeHeaderSize; result.valid_payload && offset < size; ++offset) {
        result.valid_payload =
            bytes[offset] == deterministicPayloadByte(result.sequence, offset);
    }
    return result;
}

SubscriberAccumulator::SubscriberAccumulator(std::size_t expected_size,
                                             std::uint64_t sample_stride,
                                             std::uint64_t max_samples)
    : expected_size_(expected_size), sample_stride_(sample_stride), max_samples_(max_samples) {
    measurements_.latencies.reserve(static_cast<std::size_t>(
        std::min<std::uint64_t>(max_samples_, 1000000U)));
}

void SubscriberAccumulator::observe(const void* data, std::size_t size,
                                    std::uint64_t receive_timestamp_ns) noexcept {
    const EnvelopeValidation envelope = validateEnvelope(data, size, expected_size_);
    if (!envelope.valid_size || !envelope.valid_payload ||
        receive_timestamp_ns < envelope.publish_timestamp_ns) {
        ++total_payload_errors_;
    }
    if (!envelope.valid_size) {
        return;
    }
    if (phaseFromSequence(envelope.sequence) != BenchmarkPhase::Measurement) {
        return;
    }

    ++measurements_.messages_received;
    measurements_.bytes_received += size;
    const bool correct = envelope.valid_payload &&
                         receive_timestamp_ns >= envelope.publish_timestamp_ns;
    if (correct) {
        ++measurements_.correct_messages_received;
    } else {
        ++measurements_.payload_errors;
    }

    if (have_measurement_sequence_) {
        if (envelope.sequence == last_measurement_sequence_) {
            ++measurements_.duplicate_sequences;
            ++measurements_.sequence_errors;
        } else if (envelope.sequence < last_measurement_sequence_) {
            ++measurements_.out_of_order_sequences;
            ++measurements_.sequence_errors;
        } else if (envelope.sequence > last_measurement_sequence_ + 1U) {
            measurements_.sequence_gaps += envelope.sequence - last_measurement_sequence_ - 1U;
        }
    }
    if (!have_measurement_sequence_ || envelope.sequence > last_measurement_sequence_) {
        last_measurement_sequence_ = envelope.sequence;
        have_measurement_sequence_ = true;
    }

    if (correct && (measurements_.correct_messages_received - 1U) % sample_stride_ == 0U) {
        ++measurements_.latency_samples_eligible;
        if (measurements_.latencies.size() < max_samples_) {
            measurements_.latencies.push_back(
                {envelope.sequence, receive_timestamp_ns - envelope.publish_timestamp_ns});
        } else {
            ++measurements_.latency_samples_omitted;
        }
    }
}

std::filesystem::path subscriberReadyPath(const CommonOptions& options) {
    return options.run_dir /
           ("subscriber_" + std::to_string(options.subscriber_index) + ".ready");
}

std::filesystem::path subscriberSummaryPath(const CommonOptions& options) {
    return options.run_dir /
           ("subscriber_" + std::to_string(options.subscriber_index) + ".json");
}

std::filesystem::path subscriberLatencyPath(const CommonOptions& options) {
    return options.run_dir /
           ("subscriber_" + std::to_string(options.subscriber_index) + "_latency.csv");
}

std::filesystem::path markerPath(const CommonOptions& options, std::string_view marker) {
    return options.run_dir / std::string(marker);
}

void touchFile(const std::filesystem::path& path) {
    writeTextFile(path, "ready\n");
}

bool waitForFile(const std::filesystem::path& path, std::chrono::milliseconds timeout) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::error_code error;
        if (std::filesystem::exists(path, error) && !error) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return false;
}

void writeTextFile(const std::filesystem::path& path, const std::string& contents) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot open output file " + temporary.string());
        }
        output << contents;
        if (!output) {
            throw std::runtime_error("cannot write output file " + temporary.string());
        }
    }
    std::filesystem::rename(temporary, path);
}

std::string jsonEscape(std::string_view value) {
    std::ostringstream output;
    for (const char character : value) {
        switch (character) {
        case '\\':
            output << "\\\\";
            break;
        case '"':
            output << "\\\"";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            output << character;
        }
    }
    return output.str();
}

void writeSubscriberArtifacts(const CommonOptions& options,
                              const SubscriberMeasurements& measurements,
                              std::uint64_t total_payload_errors) {
    std::ostringstream csv;
    csv << "subscriber_index,sequence,latency_ns\n";
    for (const LatencyRecord& record : measurements.latencies) {
        csv << options.subscriber_index << ',' << record.sequence << ',' << record.latency_ns
            << '\n';
    }
    writeTextFile(subscriberLatencyPath(options), csv.str());

    std::ostringstream json;
    json << "{\n"
         << "  \"role\": \"subscriber\",\n"
         << "  \"subscriber_index\": " << options.subscriber_index << ",\n"
         << "  \"messages_received\": " << measurements.messages_received << ",\n"
         << "  \"correct_messages_received\": " << measurements.correct_messages_received
         << ",\n"
         << "  \"bytes_received\": " << measurements.bytes_received << ",\n"
         << "  \"payload_errors\": " << measurements.payload_errors << ",\n"
         << "  \"total_payload_errors\": " << total_payload_errors << ",\n"
         << "  \"sequence_errors\": " << measurements.sequence_errors << ",\n"
         << "  \"sequence_gaps\": " << measurements.sequence_gaps << ",\n"
         << "  \"duplicate_sequences\": " << measurements.duplicate_sequences << ",\n"
         << "  \"out_of_order_sequences\": " << measurements.out_of_order_sequences << ",\n"
         << "  \"latency_sample_count\": " << measurements.latencies.size() << ",\n"
         << "  \"latency_samples_eligible\": " << measurements.latency_samples_eligible
         << ",\n"
         << "  \"latency_samples_omitted\": " << measurements.latency_samples_omitted << "\n"
         << "}\n";
    writeTextFile(subscriberSummaryPath(options), json.str());
}

void writePublisherArtifact(const CommonOptions& options,
                            const PublisherMeasurements& measurements,
                            std::uint64_t measurement_elapsed_ns) {
    std::ostringstream json;
    json << "{\n"
         << "  \"role\": \"publisher\",\n"
         << "  \"transport\": \"" << transportName(options.transport) << "\",\n"
         << "  \"profile\": \"" << profileName(options.profile) << "\",\n"
         << "  \"message_size_bytes\": " << options.message_size << ",\n"
         << "  \"subscriber_count\": " << options.subscriber_count << ",\n"
         << "  \"measurement_elapsed_ns\": " << measurement_elapsed_ns << ",\n"
         << "  \"publish_attempts\": " << measurements.publish_attempts << ",\n"
         << "  \"messages_published\": " << measurements.messages_published << ",\n"
         << "  \"publish_errors\": " << measurements.publish_errors << ",\n"
         << "  \"drop_count\": " << measurements.drop_count << ",\n"
         << "  \"queue_overflow_count\": " << measurements.queue_overflow_count << ",\n"
         << "  \"allocation_failure_count\": "
         << measurements.allocation_failure_count << ",\n"
         << "  \"blocked_count\": " << measurements.blocked_count << ",\n"
         << "  \"blocked_time_ns\": " << measurements.blocked_time_ns << "\n"
         << "}\n";
    writeTextFile(options.run_dir / "publisher.json", json.str());
}

} // namespace mw_benchmark
