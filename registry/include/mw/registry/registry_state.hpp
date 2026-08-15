#pragma once

#include <mw/config.hpp>
#include <mw/result.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mw::registry {

using ConnectionId = std::uint64_t;

struct NodeRecord {
    std::uint64_t node_id{0};
    std::string node_name;
    ConnectionId control_connection{0};
    std::set<std::uint64_t> publisher_endpoints;
    std::set<std::uint64_t> subscriber_endpoints;
};

struct SharedPoolMetadata {
    std::string shm_name;
    std::uint64_t pool_id{0};
    std::uint64_t segment_size{0};
    std::uint16_t layout_version{0};
};

struct SharedQueueMetadata {
    std::string shm_name;
    std::uint64_t queue_id{0};
    std::uint64_t segment_size{0};
    std::uint32_t capacity{0};
    std::uint16_t layout_version{0};
    std::uint16_t overflow_policy{0};
    std::uint64_t block_timeout_ms{0};
};

inline bool operator==(const SharedQueueMetadata& left, const SharedQueueMetadata& right) noexcept {
    return left.shm_name == right.shm_name && left.queue_id == right.queue_id &&
           left.segment_size == right.segment_size && left.capacity == right.capacity &&
           left.layout_version == right.layout_version &&
           left.overflow_policy == right.overflow_policy &&
           left.block_timeout_ms == right.block_timeout_ms;
}

inline bool operator==(const SharedPoolMetadata& left, const SharedPoolMetadata& right) noexcept {
    return left.shm_name == right.shm_name && left.pool_id == right.pool_id &&
           left.segment_size == right.segment_size && left.layout_version == right.layout_version;
}

struct TopicRecord {
    std::uint64_t topic_id{0};
    std::string topic_name;
    std::string type_name;
    std::string type_hash;
    TransportType transport{TransportType::UnixDomainSocket};
    std::size_t max_message_size{0};
    std::optional<std::uint64_t> publisher_endpoint;
    std::set<std::uint64_t> subscriber_endpoints;
    SharedPoolMetadata pool;
};

struct PublisherEndpoint {
    std::uint64_t endpoint_id{0};
    std::uint64_t node_id{0};
    std::uint64_t topic_id{0};
    std::size_t max_message_size{0};
    TransportType transport{TransportType::UnixDomainSocket};
    SharedPoolMetadata pool;
};

struct SubscriberEndpoint {
    std::uint64_t endpoint_id{0};
    std::uint64_t node_id{0};
    std::uint64_t topic_id{0};
    std::string data_socket_path;
    std::size_t max_message_size{0};
    TransportType transport{TransportType::UnixDomainSocket};
    SharedQueueMetadata queue;
};

struct IdResult {
    ErrorCode error{ErrorCode::Ok};
    std::uint64_t id{0};
};

struct EndpointResult {
    ErrorCode error{ErrorCode::Ok};
    std::uint64_t topic_id{0};
    std::uint64_t endpoint_id{0};
    SharedPoolMetadata pool;
};

struct DiscoveredSubscriber {
    std::uint64_t endpoint_id{0};
    std::string data_socket_path;
    std::size_t max_message_size{0};
    SharedQueueMetadata queue;
};

struct DiscoveryResult {
    ErrorCode error{ErrorCode::Ok};
    std::uint64_t topic_id{0};
    TransportType transport{TransportType::UnixDomainSocket};
    SharedPoolMetadata pool;
    std::vector<DiscoveredSubscriber> subscribers;
};

class RegistryState {
  public:
    IdResult registerNode(ConnectionId connection, const std::string& node_name);
    ErrorCode unregisterNode(ConnectionId connection, std::uint64_t node_id);

    EndpointResult advertise(ConnectionId connection, std::uint64_t node_id,
                             const std::string& topic_name, const std::string& type_name,
                             const std::string& type_hash, std::size_t max_message_size,
                             TransportType transport = TransportType::UnixDomainSocket,
                             SharedPoolMetadata pool = {});
    ErrorCode unadvertise(ConnectionId connection, std::uint64_t node_id,
                          std::uint64_t endpoint_id);

    EndpointResult subscribe(ConnectionId connection, std::uint64_t node_id,
                             const std::string& topic_name, const std::string& type_name,
                             const std::string& type_hash, std::size_t max_message_size,
                             const std::string& data_socket_path,
                             TransportType transport = TransportType::UnixDomainSocket,
                             SharedQueueMetadata queue = {});
    ErrorCode unsubscribe(ConnectionId connection, std::uint64_t node_id,
                          std::uint64_t endpoint_id);

    DiscoveryResult resolve(ConnectionId connection, std::uint64_t node_id,
                            std::uint64_t publisher_endpoint_id) const;

    std::vector<NodeRecord> listNodes() const;
    std::vector<TopicRecord> listTopics() const;
    std::optional<TopicRecord> queryTopic(const std::string& topic_name) const;
    std::optional<PublisherEndpoint> publisherEndpoint(std::uint64_t endpoint_id) const;

  private:
    NodeRecord* ownedNode(ConnectionId connection, std::uint64_t node_id);
    const NodeRecord* ownedNode(ConnectionId connection, std::uint64_t node_id) const;
    TopicRecord& findOrCreateTopic(const std::string& topic_name, const std::string& type_name,
                                   const std::string& type_hash, std::size_t max_message_size,
                                   TransportType transport);
    void recomputeTopicMaxMessageSize(std::uint64_t topic_id);
    void eraseTopicIfEmpty(std::uint64_t topic_id);

    std::uint64_t next_node_id_{1};
    std::uint64_t next_topic_id_{1};
    std::uint64_t next_endpoint_id_{1};
    std::map<std::uint64_t, NodeRecord> nodes_;
    std::map<std::string, std::uint64_t> node_names_;
    std::map<std::uint64_t, TopicRecord> topics_;
    std::map<std::string, std::uint64_t> topic_names_;
    std::map<std::uint64_t, PublisherEndpoint> publishers_;
    std::map<std::uint64_t, SubscriberEndpoint> subscribers_;
};

} // namespace mw::registry
