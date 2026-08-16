#pragma once

#include <mw/config.hpp>

#include "detail/control_protocol.hpp"
#include "detail/pool_protocol.hpp"
#include "detail/queue_protocol.hpp"
#include "detail/unique_fd.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mw::detail {

struct RegistryEndpoint {
    std::uint64_t topic_id{0};
    std::uint64_t endpoint_id{0};
    PoolDescriptor pool;
};

struct RegistrySubscriber {
    std::uint64_t endpoint_id{0};
    std::string data_socket_path;
    std::size_t max_message_size{0};
    QueueDescriptor queue;
};

struct RegistryDiscovery {
    std::uint64_t topic_id{0};
    TransportType transport{TransportType::UnixDomainSocket};
    PoolDescriptor pool;
    std::vector<RegistrySubscriber> subscribers;
};

struct RegistryNodeInfo {
    std::uint64_t node_id{0};
    std::string node_name;
    LivenessState liveness{LivenessState::Alive};
};

struct RegistryRegistration {
    std::uint64_t node_id{0};
    std::uint64_t session_id{0};
};

struct RegistryPeerEvent {
    PeerEventKind kind{PeerEventKind::PublisherDead};
    std::uint64_t target_endpoint_id{0};
    std::uint64_t dead_endpoint_id{0};
    std::uint64_t topic_id{0};
    std::uint64_t resource_id{0};
};

struct HeartbeatResponse {
    LivenessState state{LivenessState::Alive};
    std::vector<RegistryPeerEvent> events;
};

struct RegistryTopicInfo {
    std::uint64_t topic_id{0};
    std::string topic_name;
    std::string type_name;
    std::string type_hash;
    TransportType transport{TransportType::UnixDomainSocket};
    std::size_t max_message_size{0};
    std::size_t publisher_count{0};
    std::size_t subscriber_count{0};
    PoolDescriptor pool;
};

class RegistryClient {
  public:
    explicit RegistryClient(const RegistryConfig& config);

    RegistryClient(const RegistryClient&) = delete;
    RegistryClient& operator=(const RegistryClient&) = delete;
    RegistryClient(RegistryClient&&) noexcept = default;
    RegistryClient& operator=(RegistryClient&&) noexcept = default;

    RegistryRegistration registerNode(const std::string& node_name);
    void unregisterNode(std::uint64_t node_id);
    void attachHeartbeat(std::uint64_t node_id, std::uint64_t session_id);
    HeartbeatResponse heartbeat(std::uint64_t node_id, std::uint64_t session_id);

    RegistryEndpoint advertise(std::uint64_t node_id, const std::string& topic_name,
                               const std::string& type_name, const std::string& type_hash,
                               std::size_t max_message_size, TransportType transport,
                               const PoolDescriptor& pool = {});
    void unadvertise(std::uint64_t node_id, std::uint64_t endpoint_id);

    RegistryEndpoint subscribe(std::uint64_t node_id, const std::string& topic_name,
                               const std::string& type_name, const std::string& type_hash,
                               std::size_t max_message_size, const std::string& data_socket_path,
                               TransportType transport, const QueueDescriptor& queue = {});
    void unsubscribe(std::uint64_t node_id, std::uint64_t endpoint_id);

    RegistryDiscovery resolve(std::uint64_t node_id, std::uint64_t publisher_endpoint_id);

    std::vector<RegistryNodeInfo> listNodes();
    std::vector<RegistryTopicInfo> listTopics();
    RegistryTopicInfo queryTopic(const std::string& topic_name);

  private:
    std::vector<std::uint8_t> request(Opcode opcode, const std::vector<std::uint8_t>& payload,
                                      std::chrono::milliseconds timeout);
    ControlFrame receiveFrame(std::chrono::milliseconds timeout);

    UniqueFd socket_;
    std::chrono::milliseconds request_timeout_;
    std::uint32_t next_request_id_{1};
};

class RegistrySession {
  public:
    RegistrySession(std::string node_name, const RegistryConfig& config);
    ~RegistrySession();

    RegistrySession(const RegistrySession&) = delete;
    RegistrySession& operator=(const RegistrySession&) = delete;

    RegistryEndpoint advertise(const std::string& topic_name, const PublisherConfig& config,
                               const PoolDescriptor& pool = {});
    void unadvertise(std::uint64_t endpoint_id) noexcept;
    RegistryEndpoint subscribe(const std::string& topic_name, const SubscriberConfig& config,
                               const QueueDescriptor& queue = {});
    void unsubscribe(std::uint64_t endpoint_id) noexcept;
    RegistryDiscovery resolve(std::uint64_t publisher_endpoint_id);
    std::vector<RegistryPeerEvent> takePeerEvents(std::uint64_t target_endpoint_id);

  private:
    void heartbeatLoop() noexcept;

    RegistryClient client_;
    RegistryConfig config_;
    std::uint64_t node_id_{0};
    std::uint64_t session_id_{0};
    std::unique_ptr<RegistryClient> heartbeat_client_;
    std::thread heartbeat_thread_;
    std::mutex heartbeat_mutex_;
    std::condition_variable heartbeat_condition_;
    bool stop_heartbeat_{false};
    std::mutex event_mutex_;
    std::vector<RegistryPeerEvent> peer_events_;
};

} // namespace mw::detail
