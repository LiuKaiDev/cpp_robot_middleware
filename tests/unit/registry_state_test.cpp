#include <mw/registry/registry_state.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace {

using mw::ErrorCode;
using mw::registry::RegistryState;

TEST(RegistryStateTest, TracksNodesTopicsAndUniqueIds) {
    RegistryState state;
    const auto first = state.registerNode(10U, "camera_node");
    ASSERT_EQ(first.error, ErrorCode::Ok);
    EXPECT_EQ(state.registerNode(11U, "camera_node").error, ErrorCode::DuplicateNode);

    const auto publisher =
        state.advertise(10U, first.id, "/image", "sensor_msgs/Image", "hash1", 4096U);
    ASSERT_EQ(publisher.error, ErrorCode::Ok);
    const auto subscriber = state.subscribe(12U, 999U, "/image", "sensor_msgs/Image", "hash1",
                                            4096U, "/tmp/image.sock");
    EXPECT_EQ(subscriber.error, ErrorCode::NotRegistered);

    const auto second = state.registerNode(11U, "viewer_node");
    ASSERT_EQ(second.error, ErrorCode::Ok);
    const auto subscription = state.subscribe(11U, second.id, "/image", "sensor_msgs/Image",
                                              "hash1", 2048U, "/tmp/image_view.sock");
    ASSERT_EQ(subscription.error, ErrorCode::Ok);
    EXPECT_NE(first.id, second.id);
    EXPECT_EQ(publisher.topic_id, subscription.topic_id);
    EXPECT_NE(publisher.endpoint_id, subscription.endpoint_id);

    const auto other_topic =
        state.subscribe(11U, second.id, "/depth", "Depth", "hash2", 1024U, "/tmp/depth.sock");
    ASSERT_EQ(other_topic.error, ErrorCode::Ok);
    EXPECT_NE(other_topic.topic_id, publisher.topic_id);
    const auto high_limit_subscription = state.subscribe(
        11U, second.id, "/image", "sensor_msgs/Image", "hash1", 4096U, "/tmp/image_high.sock");
    ASSERT_EQ(high_limit_subscription.error, ErrorCode::Ok);

    const auto topics = state.listTopics();
    ASSERT_EQ(topics.size(), 2U);
    EXPECT_EQ(state.queryTopic("/image")->publisher_endpoint.has_value(), true);
    EXPECT_EQ(state.queryTopic("/image")->subscriber_endpoints.size(), 2U);
    EXPECT_EQ(state.queryTopic("/image")->max_message_size, 2048U);
    EXPECT_EQ(state.queryTopic("/image")->type_hash, "hash1");
    const auto nodes = state.listNodes();
    ASSERT_EQ(nodes.size(), 2U);
    EXPECT_EQ(nodes[0].node_name, "camera_node");
    EXPECT_EQ(nodes[1].node_name, "viewer_node");

    EXPECT_EQ(state.unsubscribe(11U, second.id, other_topic.endpoint_id), ErrorCode::Ok);
    EXPECT_FALSE(state.queryTopic("/depth").has_value());
    EXPECT_EQ(state.unsubscribe(11U, second.id, subscription.endpoint_id), ErrorCode::Ok);
    EXPECT_EQ(state.queryTopic("/image")->max_message_size, 4096U);
    EXPECT_EQ(state.unadvertise(10U, first.id, publisher.endpoint_id), ErrorCode::Ok);
    EXPECT_EQ(state.unsubscribe(11U, second.id, high_limit_subscription.endpoint_id),
              ErrorCode::Ok);
    EXPECT_FALSE(state.queryTopic("/image").has_value());
}

TEST(RegistryStateTest, EnforcesTypeCompatibilityAndOnePublisher) {
    RegistryState state;
    const auto node_a = state.registerNode(1U, "publisher");
    const auto node_b = state.registerNode(2U, "subscriber");
    ASSERT_EQ(node_a.error, ErrorCode::Ok);
    ASSERT_EQ(node_b.error, ErrorCode::Ok);

    if (std::numeric_limits<std::size_t>::max() > std::numeric_limits<std::uint32_t>::max()) {
        const auto oversized = static_cast<std::size_t>(
            static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U);
        EXPECT_EQ(state.advertise(1U, node_a.id, "/oversized", "Type", "HashA", oversized).error,
                  ErrorCode::InvalidArgument);
    }

    const auto publisher = state.advertise(1U, node_a.id, "/topic", "Type", "HashA", 1024U);
    ASSERT_EQ(publisher.error, ErrorCode::Ok);
    EXPECT_EQ(state.advertise(2U, node_b.id, "/topic", "Type", "HashA", 1024U).error,
              ErrorCode::DuplicatePublisher);
    EXPECT_EQ(state.subscribe(2U, node_b.id, "/topic", "Other", "HashA", 1024U, "/tmp/a").error,
              ErrorCode::TypeMismatch);
    EXPECT_EQ(state.subscribe(2U, node_b.id, "/topic", "Type", "HashB", 1024U, "/tmp/b").error,
              ErrorCode::TypeMismatch);

    const auto subscription =
        state.subscribe(2U, node_b.id, "/topic", "Type", "HashA", 1024U, "/tmp/c");
    ASSERT_EQ(subscription.error, ErrorCode::Ok);
    const auto discovery = state.resolve(1U, node_a.id, publisher.endpoint_id);
    EXPECT_EQ(discovery.error, ErrorCode::Ok);
    ASSERT_EQ(discovery.subscribers.size(), 1U);
    EXPECT_EQ(discovery.subscribers.front().data_socket_path, "/tmp/c");
}

TEST(RegistryStateTest, SupportsBothStartupOrdersAndCleanup) {
    RegistryState state;
    const auto subscriber_node = state.registerNode(1U, "subscriber");
    const auto subscription =
        state.subscribe(1U, subscriber_node.id, "/late", "T", "H", 512U, "/tmp/late");
    ASSERT_EQ(subscription.error, ErrorCode::Ok);
    const auto publisher_node = state.registerNode(2U, "publisher");
    const auto publisher = state.advertise(2U, publisher_node.id, "/late", "T", "H", 512U);
    ASSERT_EQ(publisher.error, ErrorCode::Ok);
    EXPECT_EQ(state.resolve(2U, publisher_node.id, publisher.endpoint_id).error, ErrorCode::Ok);

    EXPECT_EQ(state.unregisterNode(1U, subscriber_node.id), ErrorCode::Ok);
    EXPECT_EQ(state.resolve(2U, publisher_node.id, publisher.endpoint_id).error,
              ErrorCode::TopicNotFound);
    EXPECT_EQ(state.unregisterNode(2U, publisher_node.id), ErrorCode::Ok);
    EXPECT_TRUE(state.listTopics().empty());
}

TEST(RegistryStateTest, RejectsTransportMismatchAndReportsTransportInDiscovery) {
    RegistryState state;
    const auto publisher_node = state.registerNode(1U, "shm_publisher");
    const auto subscriber_node = state.registerNode(2U, "shm_subscriber");
    const auto publisher = state.advertise(1U, publisher_node.id, "/transport", "T", "H", 4096U,
                                           mw::TransportType::SharedMemory,
                                           {"/mw_registry_pool_state", 17U, 8192U, 1U});
    ASSERT_EQ(publisher.error, ErrorCode::Ok);
    EXPECT_EQ(state
                  .subscribe(2U, subscriber_node.id, "/transport", "T", "H", 4096U,
                             "/tmp/transport.sock", mw::TransportType::UnixDomainSocket)
                  .error,
              ErrorCode::TransportMismatch);

    const mw::registry::SharedQueueMetadata queue{
        "/mw_registry_queue_state",
        18U,
        4096U,
        8U,
        3U,
        static_cast<std::uint16_t>(mw::OverflowPolicy::DropOldest),
        100U};
    const auto subscriber =
        state.subscribe(2U, subscriber_node.id, "/transport", "T", "H", 4096U,
                        "/tmp/transport.sock", mw::TransportType::SharedMemory, queue);
    ASSERT_EQ(subscriber.error, ErrorCode::Ok);
    const auto discovery = state.resolve(1U, publisher_node.id, publisher.endpoint_id);
    EXPECT_EQ(discovery.error, ErrorCode::Ok);
    EXPECT_EQ(discovery.topic_id, publisher.topic_id);
    EXPECT_EQ(discovery.transport, mw::TransportType::SharedMemory);
    ASSERT_EQ(discovery.subscribers.size(), 1U);
    EXPECT_EQ(discovery.subscribers.front().queue, queue);
    EXPECT_EQ(discovery.pool.shm_name, "/mw_registry_pool_state");
    EXPECT_EQ(discovery.pool.pool_id, 17U);
    EXPECT_EQ(subscriber.pool, discovery.pool);
    EXPECT_EQ(state.queryTopic("/transport")->transport, mw::TransportType::SharedMemory);
}

} // namespace
