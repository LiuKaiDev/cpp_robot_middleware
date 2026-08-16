#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/result.hpp>

#include "detail/frame_protocol.hpp"
#include "detail/socket_io.hpp"
#include "detail/unix_socket.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using namespace std::chrono_literals;

std::string socketPath(const std::string& label) {
    static std::atomic<unsigned> counter{0};
    return "/tmp/mw_test_pubsub_" + std::to_string(::getpid()) + "_" + label + "_" +
           std::to_string(counter.fetch_add(1)) + ".sock";
}

std::vector<std::uint8_t> makePayload(std::size_t size, std::uint64_t sequence) {
    std::vector<std::uint8_t> payload(size);
    for (std::size_t offset = 0; offset < size; ++offset) {
        payload[offset] = static_cast<std::uint8_t>((sequence + offset * 31U) & 0xFFU);
    }
    return payload;
}

TEST(PubSubIntegrationTest, TransfersRequiredPayloadSizesAndMetadata) {
    for (const std::size_t size : {0U, 64U, 1024U, 1024U * 1024U}) {
        SCOPED_TRACE(size);
        const std::string path = socketPath("sizes");
        mw::Context subscriber_context{"subscriber"};
        auto subscriber = subscriber_context.createSubscriber("/test", {path, 2U * 1024U * 1024U});
        mw::Context publisher_context{"publisher"};
        auto publisher = publisher_context.createPublisher("/test", {path, 2U * 1024U * 1024U});
        const auto payload = makePayload(size, 1);

        auto publish_future = std::async(
            std::launch::async, [&] { return publisher.publish(payload.data(), payload.size()); });
        auto message = subscriber.waitAndTake(5s);
        const mw::PublishResult publish_result = publish_future.get();

        ASSERT_TRUE(publish_result) << mw::errorMessage(publish_result.error);
        ASSERT_TRUE(message.has_value()) << mw::errorMessage(subscriber.lastError());
        EXPECT_EQ(message->payload, payload);
        EXPECT_EQ(message->sequence, 1U);
        EXPECT_GT(message->publish_timestamp_ns, 0U);
    }
}

TEST(PubSubIntegrationTest, WaitAndTakeHonorsTimeout) {
    const std::string path = socketPath("timeout");
    mw::Context context{"timeout_test"};
    auto subscriber = context.createSubscriber("/test", {path, 1024});

    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(subscriber.waitAndTake(30ms).has_value());
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(subscriber.lastError(), mw::ErrorCode::Timeout);
    EXPECT_GE(elapsed, 20ms);
    EXPECT_LT(elapsed, 1s);
}

TEST(PubSubIntegrationTest, PreservesStrictSequenceAcrossManyMessages) {
    constexpr std::size_t message_count = 256;
    const std::string path = socketPath("sequence");
    mw::Context context{"sequence_test"};
    auto subscriber = context.createSubscriber("/test", {path, 4096});
    auto publisher = context.createPublisher("/test", {path, 4096});

    auto publish_future = std::async(std::launch::async, [&] {
        for (std::uint64_t sequence = 1; sequence <= message_count; ++sequence) {
            const auto payload = makePayload(64, sequence);
            const auto result = publisher.publish(payload.data(), payload.size());
            if (!result || result.sequence != sequence) {
                return false;
            }
        }
        return true;
    });

    for (std::uint64_t sequence = 1; sequence <= message_count; ++sequence) {
        auto message = subscriber.waitAndTake(5s);
        ASSERT_TRUE(message.has_value()) << "sequence " << sequence;
        EXPECT_EQ(message->sequence, sequence);
        EXPECT_EQ(message->payload, makePayload(64, sequence));
    }
    EXPECT_TRUE(publish_future.get());
}

TEST(PubSubIntegrationTest, DetectsDisconnectAndAcceptsANewPublisher) {
    const std::string path = socketPath("reconnect");
    mw::Context context{"reconnect_test"};
    auto subscriber = context.createSubscriber("/test", {path, 1024});

    {
        auto publisher = context.createPublisher("/test", {path, 1024});
        const auto payload = makePayload(64, 1);
        ASSERT_TRUE(publisher.publish(payload.data(), payload.size()));
        ASSERT_TRUE(subscriber.waitAndTake(1s).has_value());
    }

    EXPECT_FALSE(subscriber.waitAndTake(1s).has_value());
    EXPECT_EQ(subscriber.lastError(), mw::ErrorCode::ConnectionLost);

    auto publisher = context.createPublisher("/test", {path, 1024});
    const auto payload = makePayload(64, 1);
    ASSERT_TRUE(publisher.publish(payload.data(), payload.size()));
    auto message = subscriber.waitAndTake(1s);
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->sequence, 1U);
    EXPECT_EQ(message->payload, payload);
}

TEST(PubSubIntegrationTest, TakeKeepsAPartialHeaderForTheNextCall) {
    const std::string path = socketPath("partial_header");
    mw::Context context{"partial_header_test"};
    auto subscriber = context.createSubscriber("/test", {path, 1024});
    auto client = mw::detail::connectUnixSocket(path);

    const auto payload = makePayload(64, 7);
    const auto header = mw::detail::encodeFrameHeader(
        {mw::detail::kFrameMagic, static_cast<std::uint32_t>(payload.size()), 7, 99});
    ASSERT_EQ(mw::detail::writeAll(client.get(), header.data(), 8).status,
              mw::detail::IoStatus::Complete);
    EXPECT_FALSE(subscriber.take().has_value());
    EXPECT_EQ(subscriber.lastError(), mw::ErrorCode::Timeout);

    ASSERT_EQ(mw::detail::writeAll(client.get(), header.data() + 8, header.size() - 8).status,
              mw::detail::IoStatus::Complete);
    ASSERT_EQ(mw::detail::writeAll(client.get(), payload.data(), payload.size()).status,
              mw::detail::IoStatus::Complete);
    auto message = subscriber.waitAndTake(1s);
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->sequence, 7U);
    EXPECT_EQ(message->payload, payload);
}

TEST(PubSubIntegrationTest, RejectsBadMagicOversizeAndTruncatedFrames) {
    {
        const std::string path = socketPath("bad_magic");
        mw::Context context{"invalid_frame_test"};
        auto subscriber = context.createSubscriber("/test", {path, 1024});
        auto client = mw::detail::connectUnixSocket(path);
        const auto header = mw::detail::encodeFrameHeader({0, 0, 1, 1});
        ASSERT_EQ(mw::detail::writeAll(client.get(), header.data(), header.size()).status,
                  mw::detail::IoStatus::Complete);
        EXPECT_FALSE(subscriber.waitAndTake(1s).has_value());
        EXPECT_EQ(subscriber.lastError(), mw::ErrorCode::InvalidFrame);
    }
    {
        const std::string path = socketPath("oversize");
        mw::Context context{"oversize_frame_test"};
        auto subscriber = context.createSubscriber("/test", {path, 64});
        auto client = mw::detail::connectUnixSocket(path);
        const auto header = mw::detail::encodeFrameHeader({mw::detail::kFrameMagic, 65, 1, 1});
        ASSERT_EQ(mw::detail::writeAll(client.get(), header.data(), header.size()).status,
                  mw::detail::IoStatus::Complete);
        EXPECT_FALSE(subscriber.waitAndTake(1s).has_value());
        EXPECT_EQ(subscriber.lastError(), mw::ErrorCode::MessageTooLarge);
    }
    {
        const std::string path = socketPath("truncated_header");
        mw::Context context{"truncated_header_test"};
        auto subscriber = context.createSubscriber("/test", {path, 1024});
        auto client = mw::detail::connectUnixSocket(path);
        const auto header = mw::detail::encodeFrameHeader({mw::detail::kFrameMagic, 0, 1, 1});
        ASSERT_EQ(mw::detail::writeAll(client.get(), header.data(), 5).status,
                  mw::detail::IoStatus::Complete);
        client.reset();
        EXPECT_FALSE(subscriber.waitAndTake(1s).has_value());
        EXPECT_EQ(subscriber.lastError(), mw::ErrorCode::InvalidFrame);
    }
    {
        const std::string path = socketPath("truncated_payload");
        mw::Context context{"truncated_payload_test"};
        auto subscriber = context.createSubscriber("/test", {path, 1024});
        auto client = mw::detail::connectUnixSocket(path);
        const auto header = mw::detail::encodeFrameHeader({mw::detail::kFrameMagic, 10, 1, 1});
        const std::array<std::uint8_t, 3> partial_payload{1, 2, 3};
        ASSERT_EQ(mw::detail::writeAll(client.get(), header.data(), header.size()).status,
                  mw::detail::IoStatus::Complete);
        ASSERT_EQ(mw::detail::writeAll(client.get(), partial_payload.data(), partial_payload.size())
                      .status,
                  mw::detail::IoStatus::Complete);
        client.reset();
        EXPECT_FALSE(subscriber.waitAndTake(1s).has_value());
        EXPECT_EQ(subscriber.lastError(), mw::ErrorCode::InvalidFrame);
    }
}

TEST(PubSubIntegrationTest, ReportsOversizePublishAndSurvivesPeerClose) {
    const std::string path = socketPath("publish_errors");
    mw::Context context{"publish_error_test"};
    auto subscriber = std::make_unique<mw::Subscriber>(
        context.createSubscriber("/test", mw::SubscriberConfig{path, 64}));
    auto publisher = context.createPublisher("/test", {path, 64});

    const auto oversized = makePayload(65, 1);
    EXPECT_EQ(publisher.publish(oversized.data(), oversized.size()).error,
              mw::ErrorCode::MessageTooLarge);

    EXPECT_FALSE(subscriber->take().has_value());
    subscriber.reset();

    const auto payload = makePayload(64, 1);
    mw::PublishResult result;
    for (int attempt = 0; attempt < 4 && result.ok(); ++attempt) {
        result = publisher.publish(payload.data(), payload.size());
    }
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error == mw::ErrorCode::ConnectionLost ||
                result.error == mw::ErrorCode::IoError);
}

} // namespace
