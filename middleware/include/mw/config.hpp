#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mw {

inline constexpr std::size_t kDefaultMaxMessageSize = 4U * 1024U * 1024U;

enum class TransportType : std::uint16_t {
    UnixDomainSocket = 1,
    SharedMemory = 2,
};

const char* transportTypeName(TransportType transport) noexcept;

struct PublisherConfig {
    std::string socket_path;
    std::size_t max_message_size{kDefaultMaxMessageSize};
    std::string type_name{"mw.raw"};
    std::string type_hash{"mw.raw.v1"};
    TransportType transport{TransportType::UnixDomainSocket};
    std::chrono::milliseconds shm_ack_timeout{5000};
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
