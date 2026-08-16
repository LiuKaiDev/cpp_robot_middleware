#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/loaned_sample.hpp>
#include <mw/sample_view.hpp>

#include <mw/registry/registry_server.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

static_assert(!std::is_copy_constructible<mw::LoanedSample>::value);
static_assert(!std::is_copy_assignable<mw::LoanedSample>::value);
static_assert(std::is_move_constructible<mw::LoanedSample>::value);
static_assert(!std::is_copy_constructible<mw::SampleView>::value);
static_assert(!std::is_copy_assignable<mw::SampleView>::value);
static_assert(std::is_move_constructible<mw::SampleView>::value);
static_assert(std::is_same<decltype(std::declval<mw::SampleView&>().data()), const void*>::value,
              "SampleView payload access must remain read-only");

std::atomic<std::uint32_t> harness_counter{1U};

class RegistryHarness {
  public:
    explicit RegistryHarness(const std::string& label)
        : path_("/tmp/mw_phase5_" + std::to_string(::getpid()) + "_" + label + "_" +
                std::to_string(harness_counter.fetch_add(1U)) + ".sock"),
          server_(path_), thread_([this] {
              while (running_.load(std::memory_order_relaxed)) {
                  server_.pollOnce(10);
              }
          }) {}

    ~RegistryHarness() {
        running_.store(false, std::memory_order_relaxed);
        thread_.join();
    }

    mw::RegistryConfig config() const { return mw::RegistryConfig{path_}; }
    std::string dataPath(const std::string& label) const {
        return "/tmp/mw_phase5_data_" + std::to_string(::getpid()) + "_" + label + "_" +
               std::to_string(harness_counter.fetch_add(1U)) + ".sock";
    }

  private:
    std::string path_;
    mw::registry::RegistryServer server_;
    std::atomic<bool> running_{true};
    std::thread thread_;
};

mw::PublisherConfig publisherConfig(std::size_t max_size,
                                    std::vector<mw::MemoryPoolClassConfig> classes) {
    mw::PublisherConfig config;
    config.transport = mw::TransportType::SharedMemory;
    config.max_message_size = max_size;
    config.shm_ack_timeout = 2s;
    config.memory_pool.size_classes = std::move(classes);
    return config;
}

mw::SubscriberConfig subscriberConfig(const std::string& path, std::size_t max_size,
                                      std::uint32_t depth = 8U,
                                      mw::OverflowPolicy policy = mw::OverflowPolicy::DropOldest,
                                      std::chrono::milliseconds block_timeout = 100ms) {
    mw::SubscriberConfig config;
    config.socket_path = path;
    config.transport = mw::TransportType::SharedMemory;
    config.max_message_size = max_size;
    config.queue_depth = depth;
    config.overflow_policy = policy;
    config.block_timeout = block_timeout;
    return config;
}

void fill(void* raw_data, std::size_t size, std::uint8_t seed) {
    auto* data = static_cast<std::uint8_t*>(raw_data);
    for (std::size_t index = 0; index < size; ++index) {
        data[index] = static_cast<std::uint8_t>(seed + index);
    }
}

void expectPayload(const mw::SampleView& view, std::uint8_t seed) {
    const auto* data = static_cast<const std::uint8_t*>(view.data());
    ASSERT_NE(data, nullptr);
    for (std::size_t index = 0; index < view.size(); ++index) {
        ASSERT_EQ(data[index], static_cast<std::uint8_t>(seed + index)) << index;
    }
}

TEST(Phase5PublicApiTest, LoanCancellationMovesDoublePublishAndViewHoldReuse) {
    RegistryHarness registry{"lifecycle"};
    mw::Context publisher_context{"phase5_lifecycle_publisher", registry.config()};
    auto publisher = publisher_context.createPublisher(
        "/phase5/lifecycle", publisherConfig(4096U, {{4096U, 1U}}));
    mw::Context subscriber_context{"phase5_lifecycle_subscriber", registry.config()};
    auto subscriber = subscriber_context.createSubscriber(
        "/phase5/lifecycle", subscriberConfig(registry.dataPath("lifecycle"), 4096U));

    std::uint32_t cancelled_index = 0U;
    std::uint32_t cancelled_generation = 0U;
    {
        auto first = publisher.loan(256U);
        ASSERT_TRUE(first.valid());
        EXPECT_GE(first.capacity(), 256U);
        cancelled_index = first.chunkIndex();
        cancelled_generation = first.generation();
        fill(first.data(), first.size(), 3U);
        mw::LoanedSample moved = std::move(first);
        EXPECT_FALSE(first.valid());
        EXPECT_TRUE(moved.valid());
        mw::LoanedSample assigned;
        assigned = std::move(moved);
        EXPECT_FALSE(moved.valid());
        EXPECT_TRUE(assigned.valid());
    }

    EXPECT_EQ(publisher.loan(4097U).error(), mw::ErrorCode::MessageTooLarge);
    auto sample = publisher.loan(256U);
    ASSERT_TRUE(sample.valid());
    EXPECT_EQ(sample.chunkIndex(), cancelled_index);
    EXPECT_NE(sample.generation(), cancelled_generation);
    const auto identity = std::make_tuple(sample.poolId(), sample.chunkIndex(), sample.generation(),
                                          sample.payloadOffset());
    fill(sample.data(), sample.size(), 7U);
    const mw::PublishResult published = sample.publish();
    ASSERT_EQ(published.error, mw::ErrorCode::Ok);
    EXPECT_EQ(published.enqueued, 1U);
    EXPECT_EQ(sample.publish().error, mw::ErrorCode::InvalidState);

    auto view = subscriber.waitAndTakeView(2s);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(std::make_tuple(view->poolId(), view->chunkIndex(), view->generation(),
                              view->payloadOffset()),
              identity);
    EXPECT_EQ(view->sequence(), published.sequence);
    EXPECT_GT(view->publishTimestampNs(), 0U);
    expectPayload(*view, 7U);
    EXPECT_EQ(publisher.loan(256U).error(), mw::ErrorCode::PoolExhausted);

    mw::SampleView moved_view = std::move(*view);
    EXPECT_FALSE(view->valid());
    view.reset();
    expectPayload(moved_view, 7U);
    moved_view = mw::SampleView{};

    auto reused = publisher.loan(256U);
    ASSERT_TRUE(reused.valid());
    EXPECT_EQ(reused.chunkIndex(), cancelled_index);
    EXPECT_NE(reused.generation(), std::get<2>(identity));
}

TEST(Phase5PublicApiTest, SampleViewSafelyOutlivesSubscriber) {
    RegistryHarness registry{"view_lifetime"};
    mw::Context publisher_context{"phase5_view_publisher", registry.config()};
    auto publisher = publisher_context.createPublisher(
        "/phase5/view_lifetime", publisherConfig(1024U, {{1024U, 1U}}));
    std::optional<mw::SampleView> held;
    std::uint32_t chunk_index = 0U;
    std::uint32_t generation = 0U;
    {
        mw::Context subscriber_context{"phase5_view_subscriber", registry.config()};
        auto subscriber = subscriber_context.createSubscriber(
            "/phase5/view_lifetime",
            subscriberConfig(registry.dataPath("view_lifetime"), 1024U));
        auto sample = publisher.loan(512U);
        ASSERT_TRUE(sample.valid());
        chunk_index = sample.chunkIndex();
        generation = sample.generation();
        fill(sample.data(), sample.size(), 11U);
        ASSERT_EQ(sample.publish().error, mw::ErrorCode::Ok);
        held = subscriber.waitAndTakeView(2s);
        ASSERT_TRUE(held.has_value());
    }
    expectPayload(*held, 11U);
    held.reset();
    auto reused = publisher.loan(512U);
    ASSERT_TRUE(reused.valid());
    EXPECT_EQ(reused.chunkIndex(), chunk_index);
    EXPECT_NE(reused.generation(), generation);
}

TEST(Phase5PublicApiTest, OnePublisherLoansOneLogicalChunkToFourSubscribers) {
    RegistryHarness registry{"four"};
    mw::Context publisher_context{"phase5_four_publisher", registry.config()};
    auto publisher = publisher_context.createPublisher(
        "/phase5/four", publisherConfig(4096U, {{4096U, 1U}}));

    std::vector<mw::Context> contexts;
    std::vector<mw::Subscriber> subscribers;
    contexts.reserve(4U);
    subscribers.reserve(4U);
    for (std::size_t index = 0U; index < 4U; ++index) {
        contexts.emplace_back("phase5_four_subscriber_" + std::to_string(index), registry.config());
        subscribers.push_back(contexts.back().createSubscriber(
            "/phase5/four",
            subscriberConfig(registry.dataPath("four_" + std::to_string(index)), 4096U)));
    }

    auto sample = publisher.loan(2048U);
    ASSERT_TRUE(sample.valid());
    const auto identity = std::make_tuple(sample.poolId(), sample.chunkIndex(), sample.generation(),
                                          sample.payloadOffset());
    fill(sample.data(), sample.size(), 19U);
    const auto result = sample.publish();
    ASSERT_EQ(result.error, mw::ErrorCode::Ok);
    EXPECT_EQ(result.enqueued, 4U);

    std::vector<mw::SampleView> views;
    views.reserve(4U);
    for (auto& subscriber : subscribers) {
        auto view = subscriber.waitAndTakeView(2s);
        ASSERT_TRUE(view.has_value());
        EXPECT_EQ(std::make_tuple(view->poolId(), view->chunkIndex(), view->generation(),
                                  view->payloadOffset()),
                  identity);
        EXPECT_EQ(view->sequence(), result.sequence);
        expectPayload(*view, 19U);
        views.push_back(std::move(*view));
    }
    views.clear();
    EXPECT_TRUE(publisher.loan(2048U).valid());
}

TEST(Phase5PublicApiTest, ConnectedPublisherDiscoversAnAdditionalSubscriber) {
    RegistryHarness registry{"refresh"};
    mw::Context publisher_context{"phase5_refresh_publisher", registry.config()};
    auto publisher = publisher_context.createPublisher(
        "/phase5/refresh", publisherConfig(256U, {{256U, 4U}}));
    mw::Context first_context{"phase5_refresh_subscriber_1", registry.config()};
    auto first = first_context.createSubscriber(
        "/phase5/refresh", subscriberConfig(registry.dataPath("refresh_1"), 256U));

    const std::array<std::uint8_t, 32U> payload{};
    ASSERT_EQ(publisher.publish(payload.data(), payload.size()).enqueued, 1U);
    ASSERT_TRUE(first.waitAndTakeView(2s).has_value());

    mw::Context second_context{"phase5_refresh_subscriber_2", registry.config()};
    auto second = second_context.createSubscriber(
        "/phase5/refresh", subscriberConfig(registry.dataPath("refresh_2"), 256U));

    bool discovered = false;
    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto result = publisher.publish(payload.data(), payload.size());
        ASSERT_EQ(result.error, mw::ErrorCode::Ok);
        ASSERT_TRUE(first.waitAndTakeView(2s).has_value());
        if (result.enqueued == 2U) {
            ASSERT_TRUE(second.waitAndTakeView(2s).has_value());
            discovered = true;
            break;
        }
        ASSERT_EQ(result.enqueued, 1U);
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_TRUE(discovered);
}

TEST(Phase5PublicApiTest, WakeNotificationsRemainBoundedWhenQueueReturnsToEmpty) {
    RegistryHarness registry{"wake_drain"};
    mw::Context publisher_context{"phase5_wake_publisher", registry.config()};
    auto publisher = publisher_context.createPublisher(
        "/phase5/wake_drain", publisherConfig(256U, {{256U, 2U}}));
    mw::Context subscriber_context{"phase5_wake_subscriber", registry.config()};
    auto subscriber = subscriber_context.createSubscriber(
        "/phase5/wake_drain", subscriberConfig(registry.dataPath("wake_drain"), 256U));

    for (std::uint64_t sequence = 1U; sequence <= 5000U; ++sequence) {
        auto sample = publisher.loan(32U);
        ASSERT_TRUE(sample.valid()) << sequence;
        const auto result = sample.publish();
        ASSERT_EQ(result.error, mw::ErrorCode::Ok) << sequence;
        auto view = subscriber.waitAndTakeView(2s);
        ASSERT_TRUE(view.has_value()) << sequence;
        EXPECT_EQ(view->sequence(), sequence);
    }
}

TEST(Phase5BackpressureTest, DropNewestAndDropOldestPreserveExpectedSequences) {
    for (const auto policy : {mw::OverflowPolicy::DropNewest, mw::OverflowPolicy::DropOldest}) {
        RegistryHarness registry{policy == mw::OverflowPolicy::DropNewest ? "newest" : "oldest"};
        const std::string topic = policy == mw::OverflowPolicy::DropNewest
                                      ? "/phase5/drop_newest"
                                      : "/phase5/drop_oldest";
        mw::Context publisher_context{"phase5_drop_publisher", registry.config()};
        auto publisher = publisher_context.createPublisher(
            topic, publisherConfig(256U, {{256U, 4U}}));
        mw::Context subscriber_context{"phase5_drop_subscriber", registry.config()};
        auto subscriber = subscriber_context.createSubscriber(
            topic, subscriberConfig(registry.dataPath("drop"), 256U, 2U, policy));

        for (std::uint8_t seed = 1U; seed <= 3U; ++seed) {
            auto sample = publisher.loan(64U);
            ASSERT_TRUE(sample.valid());
            fill(sample.data(), sample.size(), seed);
            const auto result = sample.publish();
            if (seed < 3U || policy == mw::OverflowPolicy::DropOldest) {
                EXPECT_EQ(result.error, mw::ErrorCode::Ok);
            } else {
                EXPECT_EQ(result.error, mw::ErrorCode::QueueFull);
                EXPECT_EQ(result.dropped_newest, 1U);
            }
            if (seed == 3U && policy == mw::OverflowPolicy::DropOldest) {
                EXPECT_EQ(result.dropped_oldest, 1U);
            }
        }

        const std::uint64_t first_expected =
            policy == mw::OverflowPolicy::DropNewest ? 1U : 2U;
        auto first = subscriber.waitAndTakeView(2s);
        auto second = subscriber.takeView();
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(first->sequence(), first_expected);
        EXPECT_EQ(second->sequence(), first_expected + 1U);
    }
}

TEST(Phase5BackpressureTest, BlockPolicyReportsTimeoutAndSuccessfulWake) {
    RegistryHarness registry{"block"};
    mw::Context publisher_context{"phase5_block_publisher", registry.config()};
    auto publisher = publisher_context.createPublisher(
        "/phase5/block", publisherConfig(256U, {{256U, 4U}}));
    mw::Context subscriber_context{"phase5_block_subscriber", registry.config()};
    auto subscriber = subscriber_context.createSubscriber(
        "/phase5/block",
        subscriberConfig(registry.dataPath("block"), 256U, 1U,
                         mw::OverflowPolicy::BlockWithTimeout, 80ms));

    std::vector<std::uint8_t> payload(64U, 0x31U);
    ASSERT_EQ(publisher.publish(payload.data(), payload.size()).error, mw::ErrorCode::Ok);
    const auto start = std::chrono::steady_clock::now();
    const auto timeout_result = publisher.publish(payload.data(), payload.size());
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_EQ(timeout_result.error, mw::ErrorCode::QueueTimeout);
    EXPECT_EQ(timeout_result.block_timeouts, 1U);
    EXPECT_GE(elapsed, 50ms);

    auto blocked = std::async(std::launch::async, [&publisher, &payload] {
        return publisher.publish(payload.data(), payload.size());
    });
    EXPECT_EQ(blocked.wait_for(30ms), std::future_status::timeout);
    auto first = subscriber.waitAndTakeView(2s);
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(blocked.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(blocked.get().error, mw::ErrorCode::Ok);
    auto second = subscriber.waitAndTakeView(2s);
    ASSERT_TRUE(second.has_value());
    EXPECT_GT(second->sequence(), first->sequence());
}

TEST(Phase5PublicApiTest, LoanedLargeMessagesAndCopyPathMatch) {
    constexpr std::size_t maximum = 4U * 1024U * 1024U;
    RegistryHarness registry{"large"};
    mw::Context publisher_context{"phase5_large_publisher", registry.config()};
    auto publisher = publisher_context.createPublisher(
        "/phase5/large",
        publisherConfig(maximum, {{1024U, 2U}, {64U * 1024U, 2U},
                                  {1024U * 1024U, 2U}, {maximum, 2U}}));
    mw::Context subscriber_context{"phase5_large_subscriber", registry.config()};
    auto subscriber = subscriber_context.createSubscriber(
        "/phase5/large", subscriberConfig(registry.dataPath("large"), maximum));

    std::uint8_t seed = 23U;
    for (const std::size_t size :
         std::array<std::size_t, 4U>{1024U, 64U * 1024U, 1024U * 1024U, maximum}) {
        auto sample = publisher.loan(size);
        ASSERT_TRUE(sample.valid()) << size;
        fill(sample.data(), sample.size(), seed);
        ASSERT_EQ(sample.publish().error, mw::ErrorCode::Ok) << size;
        auto view = subscriber.waitAndTakeView(3s);
        ASSERT_TRUE(view.has_value()) << size;
        EXPECT_EQ(view->size(), size);
        expectPayload(*view, seed);
        ++seed;
    }

    std::vector<std::uint8_t> copied(4096U);
    fill(copied.data(), copied.size(), 41U);
    ASSERT_EQ(publisher.publish(copied.data(), copied.size()).error, mw::ErrorCode::Ok);
    auto copied_view = subscriber.waitAndTakeView(2s);
    ASSERT_TRUE(copied_view.has_value());
    EXPECT_EQ(std::memcmp(copied_view->data(), copied.data(), copied.size()), 0);
}

TEST(Phase5BackpressureTest, LongRunUsesBoundedQueueAcrossThousandsOfWraps) {
    RegistryHarness registry{"long"};
    mw::Context publisher_context{"phase5_long_publisher", registry.config()};
    auto publisher = publisher_context.createPublisher(
        "/phase5/long", publisherConfig(256U, {{256U, 4U}}));
    mw::Context subscriber_context{"phase5_long_subscriber", registry.config()};
    auto subscriber = subscriber_context.createSubscriber(
        "/phase5/long", subscriberConfig(registry.dataPath("long"), 256U, 3U,
                                          mw::OverflowPolicy::DropOldest));

    std::vector<std::uint8_t> payload(32U, 0x55U);
    for (std::uint64_t sequence = 1U; sequence <= 5000U; ++sequence) {
        const auto result = publisher.publish(payload.data(), payload.size());
        ASSERT_EQ(result.error, mw::ErrorCode::Ok) << sequence;
        EXPECT_EQ(result.sequence, sequence);
    }
    for (const std::uint64_t expected : {4998U, 4999U, 5000U}) {
        auto view = subscriber.waitAndTakeView(2s);
        ASSERT_TRUE(view.has_value());
        EXPECT_EQ(view->sequence(), expected);
    }
}

TEST(Phase5PublicApiTest, UdsRejectsLoanAndViewWithoutPretendingToLoan) {
    const std::string path = "/tmp/mw_phase5_uds_" + std::to_string(::getpid()) + ".sock";
    mw::Context subscriber_context{"phase5_uds_subscriber"};
    auto subscriber = subscriber_context.createSubscriber("/phase5/uds",
                                                          mw::SubscriberConfig{path, 256U});
    mw::Context publisher_context{"phase5_uds_publisher"};
    auto publisher = publisher_context.createPublisher("/phase5/uds",
                                                       mw::PublisherConfig{path, 256U});
    EXPECT_EQ(publisher.loan(32U).error(), mw::ErrorCode::UnsupportedTransport);
    EXPECT_FALSE(subscriber.takeView().has_value());
    EXPECT_EQ(subscriber.lastError(), mw::ErrorCode::UnsupportedTransport);
}

TEST(Phase5PublicApiTest, RejectsInvalidQueueConfiguration) {
    mw::Context context{"phase5_invalid_queue"};
    mw::SubscriberConfig config;
    config.socket_path = "/tmp/mw_phase5_invalid_" + std::to_string(::getpid()) + ".sock";
    config.queue_depth = 0U;
    EXPECT_THROW(context.createSubscriber("/phase5/invalid", config), std::invalid_argument);
    config.queue_depth = 1U;
    config.block_timeout = 0ms;
    EXPECT_THROW(context.createSubscriber("/phase5/invalid", config), std::invalid_argument);
}

} // namespace
