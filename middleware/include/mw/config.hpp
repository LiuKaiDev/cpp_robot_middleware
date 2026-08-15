#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mw {

inline constexpr std::size_t kDefaultMaxMessageSize = 4U * 1024U * 1024U;

enum class TransportType : std::uint16_t {
    UnixDomainSocket = 1,
    SharedMemory = 2,
};

const char* transportTypeName(TransportType transport) noexcept;

struct MemoryPoolClassConfig {
    std::size_t chunk_size{0};
    std::uint32_t chunk_count{0};
};

struct MemoryPoolConfig {
    std::vector<MemoryPoolClassConfig> size_classes{
        {256U, 32U},         {4U * 1024U, 16U},        {64U * 1024U, 8U},
        {1024U * 1024U, 4U}, {4U * 1024U * 1024U, 2U},
    };
};

struct PublisherConfig {
    PublisherConfig() = default;
    PublisherConfig(
        std::string socket_path_value, std::size_t max_message_size_value = kDefaultMaxMessageSize,
        std::string type_name_value = "mw.raw", std::string type_hash_value = "mw.raw.v1",
        TransportType transport_value = TransportType::UnixDomainSocket,
        std::chrono::milliseconds shm_ack_timeout_value = std::chrono::milliseconds{5000},
        MemoryPoolConfig memory_pool_value = {})
        : socket_path(std::move(socket_path_value)), max_message_size(max_message_size_value),
          type_name(std::move(type_name_value)), type_hash(std::move(type_hash_value)),
          transport(transport_value), shm_ack_timeout(shm_ack_timeout_value),
          memory_pool(std::move(memory_pool_value)) {}

    std::string socket_path;
    std::size_t max_message_size{kDefaultMaxMessageSize};
    std::string type_name{"mw.raw"};
    std::string type_hash{"mw.raw.v1"};
    TransportType transport{TransportType::UnixDomainSocket};
    std::chrono::milliseconds shm_ack_timeout{5000};
    MemoryPoolConfig memory_pool;
};

struct SubscriberConfig {
    std::string socket_path;
    std::size_t max_message_size{kDefaultMaxMessageSize};
    std::string type_name{"mw.raw"};
    std::string type_hash{"mw.raw.v1"};
    TransportType transport{TransportType::UnixDomainSocket};
};

struct RegistryConfig {
    std::string socket_path{"/tmp/mw_registry.sock"};
    std::chrono::milliseconds request_timeout{2000};
};

} // namespace mw
