#include "benchmark_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace {

TEST(BenchmarkPayloadTest, EncodesExactSizeAndDeterministicBytes) {
    std::array<std::uint8_t, 64> payload{};
    constexpr std::uint64_t sequence = mw_benchmark::kMeasurementSequenceBase + 7U;
    mw_benchmark::fillEnvelope(payload.data(), payload.size(), sequence, 1234U);
    const auto validation = mw_benchmark::validateEnvelope(payload.data(), payload.size(), 64U);
    EXPECT_TRUE(validation.valid_size);
    EXPECT_TRUE(validation.valid_payload);
    EXPECT_EQ(validation.sequence, sequence);
    EXPECT_EQ(validation.publish_timestamp_ns, 1234U);
    EXPECT_EQ(payload[16], mw_benchmark::deterministicPayloadByte(sequence, 16U));
}

TEST(BenchmarkPayloadTest, RejectsWrongSizeAndCorruption) {
    std::array<std::uint8_t, 64> payload{};
    mw_benchmark::fillEnvelope(payload.data(), payload.size(), 1U, 2U);
    payload[32] ^= 0x55U;
    EXPECT_FALSE(mw_benchmark::validateEnvelope(payload.data(), payload.size(), 64U).valid_payload);
    EXPECT_FALSE(mw_benchmark::validateEnvelope(payload.data(), payload.size() - 1U, 64U).valid_size);
}

TEST(BenchmarkPayloadTest, PhaseSequenceRangesAreExplicit) {
    EXPECT_EQ(mw_benchmark::phaseFromSequence(4U), mw_benchmark::BenchmarkPhase::Warmup);
    EXPECT_EQ(mw_benchmark::phaseFromSequence(mw_benchmark::kMeasurementSequenceBase + 4U),
              mw_benchmark::BenchmarkPhase::Measurement);
    EXPECT_EQ(mw_benchmark::phaseFromSequence(mw_benchmark::kCooldownSequenceBase + 4U),
              mw_benchmark::BenchmarkPhase::Cooldown);
}

TEST(BenchmarkPayloadTest, AccumulatorTracksGapsAndLatencySamples) {
    mw_benchmark::SubscriberAccumulator accumulator{64U, 1U, 16U};
    std::array<std::uint8_t, 64> payload{};
    for (const std::uint64_t local : {1U, 3U, 3U}) {
        mw_benchmark::fillEnvelope(payload.data(), payload.size(),
                                   mw_benchmark::kMeasurementSequenceBase + local, 100U);
        accumulator.observe(payload.data(), payload.size(), 200U);
    }
    const auto& measurements = accumulator.measurements();
    EXPECT_EQ(measurements.messages_received, 3U);
    EXPECT_EQ(measurements.sequence_gaps, 1U);
    EXPECT_EQ(measurements.duplicate_sequences, 1U);
    EXPECT_EQ(measurements.sequence_errors, 1U);
    EXPECT_EQ(measurements.latencies.size(), 3U);
}

} // namespace
