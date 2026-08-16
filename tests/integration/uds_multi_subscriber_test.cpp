#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/registry/registry_server.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace std::chrono_literals;

TEST(UdsMultiSubscriberIntegrationTest, FansOutOneFrameToFourRealEndpoints) {
    const std::string token = std::to_string(::getpid());
    const std::string registry_path = "/tmp/mw_test_uds_fanout_registry_" + token + ".sock";
    std::atomic<bool> stop{false};
    std::thread registry_thread([&] {
        mw::registry::RegistryServer server{registry_path};
        while (!stop.load(std::memory_order_relaxed)) {
            server.pollOnce(10);
        }
    });
    const auto registry_deadline = std::chrono::steady_clock::now() + 2s;
    while (!std::filesystem::exists(registry_path) &&
           std::chrono::steady_clock::now() < registry_deadline) {
        std::this_thread::sleep_for(5ms);
    }
    ASSERT_TRUE(std::filesystem::exists(registry_path));

    {
        std::vector<std::unique_ptr<mw::Context>> contexts;
        std::vector<std::unique_ptr<mw::Subscriber>> subscribers;
        contexts.reserve(4U);
        subscribers.reserve(4U);
        for (std::size_t index = 0U; index < 4U; ++index) {
            contexts.push_back(
                std::make_unique<mw::Context>("uds_fanout_subscriber_" + std::to_string(index),
                                              mw::RegistryConfig{registry_path}));
            mw::SubscriberConfig config;
            config.socket_path =
                "/tmp/mw_test_uds_fanout_data_" + token + "_" + std::to_string(index) + ".sock";
            config.max_message_size = 64U;
            config.type_name = "mw.benchmark.Bytes";
            config.type_hash = "mw.benchmark.Bytes.v1";
            config.transport = mw::TransportType::UnixDomainSocket;
            subscribers.push_back(std::make_unique<mw::Subscriber>(
                contexts.back()->createSubscriber("/test/uds_fanout", config)));
        }

        mw::Context publisher_context{"uds_fanout_publisher", mw::RegistryConfig{registry_path}};
        mw::PublisherConfig publisher_config;
        publisher_config.socket_path = "/tmp/mw_test_uds_fanout_publisher_" + token + ".sock";
        publisher_config.max_message_size = 64U;
        publisher_config.type_name = "mw.benchmark.Bytes";
        publisher_config.type_hash = "mw.benchmark.Bytes.v1";
        publisher_config.transport = mw::TransportType::UnixDomainSocket;
        auto publisher = publisher_context.createPublisher("/test/uds_fanout", publisher_config);

        std::vector<std::optional<mw::ReceivedMessage>> received(4U);
        std::vector<std::thread> receivers;
        for (std::size_t index = 0U; index < subscribers.size(); ++index) {
            receivers.emplace_back(
                [&, index] { received[index] = subscribers[index]->waitAndTake(2s); });
        }
        std::array<std::uint8_t, 64> payload{};
        for (std::size_t index = 0U; index < payload.size(); ++index) {
            payload[index] = static_cast<std::uint8_t>(index + 3U);
        }
        const auto result = publisher.publish(payload.data(), payload.size());
        EXPECT_TRUE(result);
        EXPECT_EQ(result.enqueued, 4U);
        for (auto& receiver : receivers) {
            receiver.join();
        }
        for (const auto& message : received) {
            ASSERT_TRUE(message.has_value());
            EXPECT_EQ(message->payload.size(), payload.size());
            EXPECT_EQ(message->payload, std::vector<std::uint8_t>(payload.begin(), payload.end()));
        }
    }
    stop.store(true, std::memory_order_relaxed);
    registry_thread.join();
    std::error_code error;
    std::filesystem::remove(registry_path, error);
}

} // namespace
