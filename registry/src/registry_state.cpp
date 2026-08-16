#include <mw/registry/registry_state.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mw::registry {
namespace {

bool validTransport(TransportType transport) noexcept {
    return transport == TransportType::UnixDomainSocket || transport == TransportType::SharedMemory;
}

bool validPoolMetadata(TransportType transport, const SharedPoolMetadata& pool) noexcept {
    if (transport == TransportType::UnixDomainSocket) {
        return pool.shm_name.empty() && pool.pool_id == 0U && pool.segment_size == 0U &&
               pool.layout_version == 0U;
    }
    return pool.shm_name.size() >= 2U && pool.shm_name.size() <= 192U &&
           pool.shm_name.front() == '/' && pool.shm_name.find('\0') == std::string::npos &&
           pool.shm_name.find('/', 1U) == std::string::npos && pool.pool_id != 0U &&
           pool.segment_size != 0U && pool.layout_version == 1U;
}

bool validQueueMetadata(TransportType transport, const SharedQueueMetadata& queue) noexcept {
    if (transport == TransportType::UnixDomainSocket) {
        return queue.shm_name.empty() && queue.queue_id == 0U && queue.segment_size == 0U &&
               queue.capacity == 0U && queue.layout_version == 0U && queue.overflow_policy == 0U &&
               queue.block_timeout_ms == 0U;
    }
    const bool valid_policy =
        queue.overflow_policy >= static_cast<std::uint16_t>(OverflowPolicy::DropNewest) &&
        queue.overflow_policy <= static_cast<std::uint16_t>(OverflowPolicy::BlockWithTimeout);
    return queue.shm_name.size() >= 2U && queue.shm_name.size() <= 192U &&
           queue.shm_name.front() == '/' && queue.shm_name.find('\0') == std::string::npos &&
           queue.shm_name.find('/', 1U) == std::string::npos && queue.queue_id != 0U &&
           queue.segment_size != 0U && queue.capacity != 0U && queue.capacity <= 65536U &&
           queue.layout_version == 2U && valid_policy && queue.block_timeout_ms != 0U;
}

} // namespace

RegistryState::RegistryState(LivenessConfig liveness) : liveness_(liveness) {
    if (!validLivenessConfig(liveness_)) {
        throw std::invalid_argument("invalid registry liveness timing");
    }
}

IdResult RegistryState::registerNode(ConnectionId connection, const std::string& node_name,
                                     MonotonicTime now) {
    if (node_name.empty()) {
        return {ErrorCode::InvalidArgument, 0, 0};
    }
    if (node_names_.count(node_name) != 0U) {
        return {ErrorCode::DuplicateNode, 0, 0};
    }

    const std::uint64_t node_id = next_node_id_++;
    const std::uint64_t session_id = next_session_id_++;
    NodeRecord node;
    node.node_id = node_id;
    node.session_id = session_id;
    node.node_name = node_name;
    node.control_connection = connection;
    node.last_seen = now;
    nodes_.emplace(node_id, std::move(node));
    node_names_.emplace(node_name, node_id);
    return {ErrorCode::Ok, node_id, session_id};
}

ErrorCode RegistryState::unregisterNode(ConnectionId connection, std::uint64_t node_id) {
    NodeRecord* node = ownedNode(connection, node_id);
    if (node == nullptr) {
        return ErrorCode::NotRegistered;
    }

    const auto publishers = node->publisher_endpoints;
    const auto subscribers = node->subscriber_endpoints;
    for (const std::uint64_t endpoint_id : publishers) {
        (void)unadvertise(connection, node_id, endpoint_id);
    }
    for (const std::uint64_t endpoint_id : subscribers) {
        (void)unsubscribe(connection, node_id, endpoint_id);
    }

    node_names_.erase(node->node_name);
    nodes_.erase(node_id);
    return ErrorCode::Ok;
}

ErrorCode RegistryState::attachHeartbeat(ConnectionId connection, std::uint64_t node_id,
                                         std::uint64_t session_id) {
    const auto iterator = nodes_.find(node_id);
    if (iterator == nodes_.end() || iterator->second.session_id != session_id || connection == 0U) {
        return ErrorCode::NotRegistered;
    }
    NodeRecord& node = iterator->second;
    if (node.heartbeat_connection != 0U && node.heartbeat_connection != connection) {
        return ErrorCode::NotRegistered;
    }
    node.heartbeat_connection = connection;
    return ErrorCode::Ok;
}

HeartbeatResult RegistryState::heartbeat(ConnectionId connection, std::uint64_t node_id,
                                         std::uint64_t session_id, MonotonicTime now) {
    const auto iterator = nodes_.find(node_id);
    if (iterator == nodes_.end() || iterator->second.session_id != session_id) {
        return {ErrorCode::NotRegistered, LivenessState::Dead};
    }
    NodeRecord& node = iterator->second;
    if (connection != node.control_connection && connection != node.heartbeat_connection) {
        return {ErrorCode::NotRegistered, node.liveness};
    }
    node.last_seen = now;
    node.liveness = LivenessState::Alive;
    ++metrics_.heartbeat_received;
    return {ErrorCode::Ok, node.liveness};
}

LivenessUpdate RegistryState::evaluateLiveness(MonotonicTime now) {
    LivenessUpdate update;
    std::vector<std::uint64_t> dead_ids;
    for (auto& entry : nodes_) {
        NodeRecord& node = entry.second;
        const auto elapsed = now - node.last_seen;
        if (elapsed >= liveness_.suspect_timeout && node.liveness == LivenessState::Alive) {
            node.liveness = LivenessState::Suspected;
            ++metrics_.suspected_count;
            if (elapsed < liveness_.dead_timeout) {
                update.suspected_nodes.push_back(node.node_id);
            }
        }
        if (elapsed >= liveness_.dead_timeout) {
            dead_ids.push_back(node.node_id);
        }
    }
    for (const std::uint64_t node_id : dead_ids) {
        if (auto cleanup = cleanupDeadNode(node_id); cleanup.has_value()) {
            update.dead_nodes.push_back(std::move(*cleanup));
        }
    }
    return update;
}

std::vector<DeadNodeCleanup> RegistryState::disconnectConnection(ConnectionId connection) {
    std::vector<std::uint64_t> node_ids;
    for (const auto& entry : nodes_) {
        if (entry.second.control_connection == connection) {
            node_ids.push_back(entry.first);
        }
    }
    std::vector<DeadNodeCleanup> cleanups;
    cleanups.reserve(node_ids.size());
    for (const std::uint64_t node_id : node_ids) {
        if (auto cleanup = cleanupDeadNode(node_id); cleanup.has_value()) {
            cleanups.push_back(std::move(*cleanup));
        }
    }
    if (cleanups.empty()) {
        detachHeartbeatConnection(connection);
    }
    return cleanups;
}

void RegistryState::detachHeartbeatConnection(ConnectionId connection) noexcept {
    for (auto& entry : nodes_) {
        if (entry.second.heartbeat_connection == connection) {
            entry.second.heartbeat_connection = 0U;
        }
    }
}

EndpointResult RegistryState::advertise(ConnectionId connection, std::uint64_t node_id,
                                        const std::string& topic_name, const std::string& type_name,
                                        const std::string& type_hash, std::size_t max_message_size,
                                        TransportType transport, SharedPoolMetadata pool) {
    NodeRecord* node = ownedNode(connection, node_id);
    if (node == nullptr) {
        return {ErrorCode::NotRegistered, 0, 0, {}};
    }
    if (topic_name.empty() || type_name.empty() || type_hash.empty() ||
        max_message_size > std::numeric_limits<std::uint32_t>::max() ||
        !validTransport(transport) || !validPoolMetadata(transport, pool)) {
        return {ErrorCode::InvalidArgument, 0, 0, {}};
    }

    const auto topic_iterator = topic_names_.find(topic_name);
    if (topic_iterator != topic_names_.end()) {
        TopicRecord& topic = topics_.at(topic_iterator->second);
        if (topic.type_name != type_name || topic.type_hash != type_hash) {
            return {ErrorCode::TypeMismatch, topic.topic_id, 0, {}};
        }
        if (topic.transport != transport) {
            return {ErrorCode::TransportMismatch, topic.topic_id, 0, {}};
        }
        if (topic.publisher_endpoint.has_value()) {
            return {ErrorCode::DuplicatePublisher, topic.topic_id, 0, {}};
        }
    }

    TopicRecord& topic =
        findOrCreateTopic(topic_name, type_name, type_hash, max_message_size, transport);
    const std::uint64_t endpoint_id = next_endpoint_id_++;
    publishers_.emplace(endpoint_id, PublisherEndpoint{endpoint_id, node_id, topic.topic_id,
                                                       max_message_size, transport, pool});
    topic.publisher_endpoint = endpoint_id;
    topic.pool = pool;
    topic.max_message_size = std::min(topic.max_message_size, max_message_size);
    node->publisher_endpoints.insert(endpoint_id);
    return {ErrorCode::Ok, topic.topic_id, endpoint_id, std::move(pool)};
}

ErrorCode RegistryState::unadvertise(ConnectionId connection, std::uint64_t node_id,
                                     std::uint64_t endpoint_id) {
    NodeRecord* node = ownedNode(connection, node_id);
    const auto endpoint_iterator = publishers_.find(endpoint_id);
    if (node == nullptr || endpoint_iterator == publishers_.end() ||
        endpoint_iterator->second.node_id != node_id) {
        return ErrorCode::EndpointNotFound;
    }

    const std::uint64_t topic_id = endpoint_iterator->second.topic_id;
    node->publisher_endpoints.erase(endpoint_id);
    publishers_.erase(endpoint_iterator);
    topics_.at(topic_id).publisher_endpoint.reset();
    topics_.at(topic_id).pool = {};
    recomputeTopicMaxMessageSize(topic_id);
    eraseTopicIfEmpty(topic_id);
    return ErrorCode::Ok;
}

EndpointResult RegistryState::subscribe(ConnectionId connection, std::uint64_t node_id,
                                        const std::string& topic_name, const std::string& type_name,
                                        const std::string& type_hash, std::size_t max_message_size,
                                        const std::string& data_socket_path,
                                        TransportType transport, SharedQueueMetadata queue) {
    NodeRecord* node = ownedNode(connection, node_id);
    if (node == nullptr) {
        return {ErrorCode::NotRegistered, 0, 0, {}};
    }
    if (topic_name.empty() || type_name.empty() || type_hash.empty() || data_socket_path.empty() ||
        max_message_size > std::numeric_limits<std::uint32_t>::max() ||
        !validTransport(transport) || !validQueueMetadata(transport, queue)) {
        return {ErrorCode::InvalidArgument, 0, 0, {}};
    }

    const auto topic_iterator = topic_names_.find(topic_name);
    if (topic_iterator != topic_names_.end()) {
        const TopicRecord& topic = topics_.at(topic_iterator->second);
        if (topic.type_name != type_name || topic.type_hash != type_hash) {
            return {ErrorCode::TypeMismatch, topic.topic_id, 0, {}};
        }
        if (topic.transport != transport) {
            return {ErrorCode::TransportMismatch, topic.topic_id, 0, {}};
        }
    }

    TopicRecord& topic =
        findOrCreateTopic(topic_name, type_name, type_hash, max_message_size, transport);
    const std::uint64_t endpoint_id = next_endpoint_id_++;
    subscribers_.emplace(endpoint_id,
                         SubscriberEndpoint{endpoint_id, node_id, topic.topic_id, data_socket_path,
                                            max_message_size, transport, std::move(queue)});
    topic.subscriber_endpoints.insert(endpoint_id);
    topic.max_message_size = std::min(topic.max_message_size, max_message_size);
    node->subscriber_endpoints.insert(endpoint_id);
    return {ErrorCode::Ok, topic.topic_id, endpoint_id, topic.pool};
}

ErrorCode RegistryState::unsubscribe(ConnectionId connection, std::uint64_t node_id,
                                     std::uint64_t endpoint_id) {
    NodeRecord* node = ownedNode(connection, node_id);
    const auto endpoint_iterator = subscribers_.find(endpoint_id);
    if (node == nullptr || endpoint_iterator == subscribers_.end() ||
        endpoint_iterator->second.node_id != node_id) {
        return ErrorCode::EndpointNotFound;
    }

    const std::uint64_t topic_id = endpoint_iterator->second.topic_id;
    node->subscriber_endpoints.erase(endpoint_id);
    subscribers_.erase(endpoint_iterator);
    topics_.at(topic_id).subscriber_endpoints.erase(endpoint_id);
    recomputeTopicMaxMessageSize(topic_id);
    eraseTopicIfEmpty(topic_id);
    return ErrorCode::Ok;
}

DiscoveryResult RegistryState::resolve(ConnectionId connection, std::uint64_t node_id,
                                       std::uint64_t publisher_endpoint_id) const {
    const NodeRecord* node = ownedNode(connection, node_id);
    const auto publisher_iterator = publishers_.find(publisher_endpoint_id);
    if (node == nullptr || publisher_iterator == publishers_.end() ||
        publisher_iterator->second.node_id != node_id) {
        return {ErrorCode::EndpointNotFound, 0, TransportType::UnixDomainSocket, {}, {}};
    }

    const TopicRecord& topic = topics_.at(publisher_iterator->second.topic_id);
    if (topic.subscriber_endpoints.empty()) {
        return {ErrorCode::TopicNotFound, topic.topic_id, topic.transport, topic.pool, {}};
    }
    DiscoveryResult result{ErrorCode::Ok, topic.topic_id, topic.transport, topic.pool, {}};
    result.subscribers.reserve(topic.subscriber_endpoints.size());
    for (const std::uint64_t endpoint_id : topic.subscriber_endpoints) {
        const SubscriberEndpoint& subscriber = subscribers_.at(endpoint_id);
        result.subscribers.push_back({subscriber.endpoint_id, subscriber.data_socket_path,
                                      subscriber.max_message_size, subscriber.queue});
    }
    return result;
}

std::vector<NodeRecord> RegistryState::listNodes() const {
    std::vector<NodeRecord> records;
    records.reserve(nodes_.size());
    for (const auto& entry : nodes_) {
        records.push_back(entry.second);
    }
    std::sort(records.begin(), records.end(), [](const NodeRecord& left, const NodeRecord& right) {
        return left.node_name < right.node_name;
    });
    return records;
}

std::vector<TopicRecord> RegistryState::listTopics() const {
    std::vector<TopicRecord> records;
    records.reserve(topics_.size());
    for (const auto& entry : topics_) {
        records.push_back(entry.second);
    }
    std::sort(records.begin(), records.end(),
              [](const TopicRecord& left, const TopicRecord& right) {
                  return left.topic_name < right.topic_name;
              });
    return records;
}

std::optional<TopicRecord> RegistryState::queryTopic(const std::string& topic_name) const {
    const auto iterator = topic_names_.find(topic_name);
    if (iterator == topic_names_.end()) {
        return std::nullopt;
    }
    return topics_.at(iterator->second);
}

std::optional<PublisherEndpoint> RegistryState::publisherEndpoint(std::uint64_t endpoint_id) const {
    const auto iterator = publishers_.find(endpoint_id);
    if (iterator == publishers_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

std::optional<NodeRecord> RegistryState::node(std::uint64_t node_id) const {
    const auto iterator = nodes_.find(node_id);
    return iterator == nodes_.end() ? std::nullopt : std::optional<NodeRecord>{iterator->second};
}

NodeRecord* RegistryState::ownedNode(ConnectionId connection, std::uint64_t node_id) {
    const auto iterator = nodes_.find(node_id);
    if (iterator == nodes_.end() || iterator->second.control_connection != connection) {
        return nullptr;
    }
    return &iterator->second;
}

const NodeRecord* RegistryState::ownedNode(ConnectionId connection, std::uint64_t node_id) const {
    const auto iterator = nodes_.find(node_id);
    if (iterator == nodes_.end() || iterator->second.control_connection != connection) {
        return nullptr;
    }
    return &iterator->second;
}

TopicRecord& RegistryState::findOrCreateTopic(const std::string& topic_name,
                                              const std::string& type_name,
                                              const std::string& type_hash,
                                              std::size_t max_message_size,
                                              TransportType transport) {
    const auto iterator = topic_names_.find(topic_name);
    if (iterator != topic_names_.end()) {
        return topics_.at(iterator->second);
    }

    const std::uint64_t topic_id = next_topic_id_++;
    topics_.emplace(topic_id, TopicRecord{topic_id,
                                          topic_name,
                                          type_name,
                                          type_hash,
                                          transport,
                                          max_message_size,
                                          std::nullopt,
                                          {},
                                          {}});
    topic_names_.emplace(topic_name, topic_id);
    return topics_.at(topic_id);
}

void RegistryState::recomputeTopicMaxMessageSize(std::uint64_t topic_id) {
    TopicRecord& topic = topics_.at(topic_id);
    std::size_t maximum = std::numeric_limits<std::size_t>::max();
    bool has_endpoint = false;

    if (topic.publisher_endpoint.has_value()) {
        maximum = std::min(maximum, publishers_.at(*topic.publisher_endpoint).max_message_size);
        has_endpoint = true;
    }
    for (const std::uint64_t endpoint_id : topic.subscriber_endpoints) {
        maximum = std::min(maximum, subscribers_.at(endpoint_id).max_message_size);
        has_endpoint = true;
    }
    if (has_endpoint) {
        topic.max_message_size = maximum;
    }
}

void RegistryState::eraseTopicIfEmpty(std::uint64_t topic_id) {
    const auto iterator = topics_.find(topic_id);
    if (iterator == topics_.end() || iterator->second.publisher_endpoint.has_value() ||
        !iterator->second.subscriber_endpoints.empty()) {
        return;
    }
    topic_names_.erase(iterator->second.topic_name);
    topics_.erase(iterator);
}

std::optional<DeadNodeCleanup> RegistryState::cleanupDeadNode(std::uint64_t node_id) {
    const auto node_iterator = nodes_.find(node_id);
    if (node_iterator == nodes_.end()) {
        return std::nullopt;
    }

    const NodeRecord node = node_iterator->second;
    DeadNodeCleanup cleanup;
    cleanup.node_id = node.node_id;
    cleanup.session_id = node.session_id;
    cleanup.control_connection = node.control_connection;
    cleanup.heartbeat_connection = node.heartbeat_connection;

    for (const std::uint64_t endpoint_id : node.publisher_endpoints) {
        const auto publisher_iterator = publishers_.find(endpoint_id);
        if (publisher_iterator == publishers_.end()) {
            continue;
        }
        const PublisherEndpoint& publisher = publisher_iterator->second;
        if (!publisher.pool.shm_name.empty()) {
            cleanup.pools.push_back(publisher.pool);
        }
        const TopicRecord& topic = topics_.at(publisher.topic_id);
        for (const std::uint64_t subscriber_id : topic.subscriber_endpoints) {
            const auto subscriber_iterator = subscribers_.find(subscriber_id);
            if (subscriber_iterator != subscribers_.end()) {
                cleanup.peer_events.push_back(
                    {PeerEventKind::PublisherDead, subscriber_iterator->second.node_id,
                     subscriber_id, endpoint_id, publisher.topic_id, publisher.pool.pool_id});
            }
        }
    }

    for (const std::uint64_t endpoint_id : node.subscriber_endpoints) {
        const auto subscriber_iterator = subscribers_.find(endpoint_id);
        if (subscriber_iterator == subscribers_.end()) {
            continue;
        }
        const SubscriberEndpoint& subscriber = subscriber_iterator->second;
        if (!subscriber.queue.shm_name.empty()) {
            cleanup.queues.push_back(subscriber.queue);
        }
        cleanup.socket_paths.push_back(subscriber.data_socket_path);
        const TopicRecord& topic = topics_.at(subscriber.topic_id);
        if (topic.publisher_endpoint.has_value()) {
            const auto publisher_iterator = publishers_.find(*topic.publisher_endpoint);
            if (publisher_iterator != publishers_.end()) {
                cleanup.peer_events.push_back({PeerEventKind::SubscriberDead,
                                               publisher_iterator->second.node_id,
                                               publisher_iterator->second.endpoint_id, endpoint_id,
                                               subscriber.topic_id, subscriber.queue.queue_id});
            }
        }
    }

    (void)unregisterNode(node.control_connection, node.node_id);
    ++metrics_.dead_node_count;
    return cleanup;
}

} // namespace mw::registry
