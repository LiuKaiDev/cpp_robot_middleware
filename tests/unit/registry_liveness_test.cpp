#include <mw/registry/registry_state.hpp>

#include <gtest/gtest.h>

#include <chrono>

namespace {

using namespace std::chrono_literals;
using mw::ErrorCode;
using mw::LivenessState;
using mw::registry::MonotonicTime;
using mw::registry::RegistryState;

TEST(RegistryLivenessTest, TransitionsRecoverAndDeadSessionIsTerminal) {
    RegistryState state{{10ms, 20ms, 40ms}};
    const MonotonicTime start{100ms};
    const auto registration = state.registerNode(11U, "camera", start);
    ASSERT_EQ(registration.error, ErrorCode::Ok);
    ASSERT_NE(registration.session_id, 0U);
    EXPECT_EQ(state.attachHeartbeat(12U, registration.id, registration.session_id), ErrorCode::Ok);
    EXPECT_EQ(state.heartbeat(13U, registration.id, registration.session_id, start + 5ms).error,
              ErrorCode::NotRegistered);
    EXPECT_EQ(state.heartbeat(12U, registration.id, registration.session_id, start + 5ms).error,
              ErrorCode::Ok);

    const auto suspected = state.evaluateLiveness(start + 26ms);
    EXPECT_EQ(suspected.suspected_nodes, std::vector<std::uint64_t>{registration.id});
    ASSERT_TRUE(state.node(registration.id).has_value());
    EXPECT_EQ(state.node(registration.id)->liveness, LivenessState::Suspected);

    EXPECT_EQ(state.heartbeat(12U, registration.id, registration.session_id, start + 30ms).state,
              LivenessState::Alive);
    EXPECT_EQ(state.node(registration.id)->liveness, LivenessState::Alive);

    EXPECT_TRUE(state.evaluateLiveness(start + 51ms).dead_nodes.empty());
    const auto dead = state.evaluateLiveness(start + 71ms);
    ASSERT_EQ(dead.dead_nodes.size(), 1U);
    EXPECT_EQ(dead.dead_nodes.front().node_id, registration.id);
    EXPECT_FALSE(state.node(registration.id).has_value());
    EXPECT_EQ(state.heartbeat(12U, registration.id, registration.session_id, start + 72ms).error,
              ErrorCode::NotRegistered);
    EXPECT_TRUE(state.disconnectConnection(11U).empty());

    const auto replacement = state.registerNode(21U, "camera", start + 73ms);
    EXPECT_EQ(replacement.error, ErrorCode::Ok);
    EXPECT_NE(replacement.id, registration.id);
    EXPECT_NE(replacement.session_id, registration.session_id);
    EXPECT_EQ(state.metrics().heartbeat_received, 2U);
    EXPECT_EQ(state.metrics().suspected_count, 2U);
    EXPECT_EQ(state.metrics().dead_node_count, 1U);
    const auto stats = state.statsSnapshot();
    EXPECT_EQ(stats.node_count, 1U);
    EXPECT_EQ(stats.topic_count, 0U);
    EXPECT_EQ(stats.publisher_count, 0U);
    EXPECT_EQ(stats.subscriber_count, 0U);
    EXPECT_EQ(stats.heartbeat_received, 2U);
    EXPECT_EQ(stats.suspected_count, 2U);
    EXPECT_EQ(stats.dead_node_count, 1U);
}

TEST(RegistryLivenessTest, DeadCleanupReturnsExactResourcesAndPeerEvents) {
    RegistryState state{{10ms, 20ms, 40ms}};
    const MonotonicTime start{200ms};
    const auto publisher_node = state.registerNode(1U, "publisher", start);
    const auto subscriber_node = state.registerNode(2U, "subscriber", start);
    const auto publisher =
        state.advertise(1U, publisher_node.id, "/fault", "T", "H", 4096U,
                        mw::TransportType::SharedMemory, {"/mw_p5_liveness", 101U, 8192U, 1U});
    ASSERT_EQ(publisher.error, ErrorCode::Ok);
    const mw::registry::SharedQueueMetadata queue{
        "/mw_q5_liveness",
        202U,
        4096U,
        4U,
        3U,
        static_cast<std::uint16_t>(mw::OverflowPolicy::BlockWithTimeout),
        100U};
    const auto subscriber =
        state.subscribe(2U, subscriber_node.id, "/fault", "T", "H", 4096U, "/tmp/mw_liveness.sock",
                        mw::TransportType::SharedMemory, queue);
    ASSERT_EQ(subscriber.error, ErrorCode::Ok);

    const auto subscriber_cleanup = state.disconnectConnection(2U);
    ASSERT_EQ(subscriber_cleanup.size(), 1U);
    ASSERT_EQ(subscriber_cleanup.front().queues.size(), 1U);
    EXPECT_EQ(subscriber_cleanup.front().queues.front(), queue);
    EXPECT_EQ(subscriber_cleanup.front().socket_paths,
              std::vector<std::string>{"/tmp/mw_liveness.sock"});
    ASSERT_EQ(subscriber_cleanup.front().peer_events.size(), 1U);
    EXPECT_EQ(subscriber_cleanup.front().peer_events.front().target_endpoint_id,
              publisher.endpoint_id);
    EXPECT_EQ(subscriber_cleanup.front().peer_events.front().dead_endpoint_id,
              subscriber.endpoint_id);
    EXPECT_TRUE(state.disconnectConnection(2U).empty());

    const auto replacement_subscriber = state.registerNode(3U, "subscriber", start + 1ms);
    EXPECT_EQ(replacement_subscriber.error, ErrorCode::Ok);
    EXPECT_EQ(state
                  .advertise(3U, replacement_subscriber.id, "/fault", "T", "H", 4096U,
                             mw::TransportType::SharedMemory, {"/mw_p5_duplicate", 303U, 8192U, 1U})
                  .error,
              ErrorCode::DuplicatePublisher);

    const auto publisher_cleanup = state.disconnectConnection(1U);
    ASSERT_EQ(publisher_cleanup.size(), 1U);
    ASSERT_EQ(publisher_cleanup.front().pools.size(), 1U);
    EXPECT_EQ(publisher_cleanup.front().pools.front().shm_name, "/mw_p5_liveness");
    const auto replacement_publisher = state.registerNode(4U, "publisher", start + 2ms);
    ASSERT_EQ(replacement_publisher.error, ErrorCode::Ok);
    EXPECT_EQ(state
                  .advertise(4U, replacement_publisher.id, "/fault", "T", "H", 4096U,
                             mw::TransportType::SharedMemory,
                             {"/mw_p5_replacement", 404U, 8192U, 1U})
                  .error,
              ErrorCode::Ok);
}

TEST(RegistryLivenessTest, RejectsInvalidTimingConfiguration) {
    EXPECT_THROW((RegistryState{{20ms, 20ms, 40ms}}), std::invalid_argument);
    EXPECT_THROW((RegistryState{{20ms, 10ms, 40ms}}), std::invalid_argument);
    EXPECT_THROW((RegistryState{{10ms, 40ms, 40ms}}), std::invalid_argument);
}

} // namespace
