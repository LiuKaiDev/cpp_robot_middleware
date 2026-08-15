#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace mw {

inline constexpr std::size_t kDefaultMaxMessageSize = 4U * 1024U * 1024U;

struct PublisherConfig {
    std::string socket_path;
    std::size_t max_message_size{kDefaultMaxMessageSize};
    std::string type_name{"mw.raw"};
    std::string type_hash{"mw.raw.v1"};
};

struct SubscriberConfig {
    std::string socket_path;
    std::size_t max_message_size{kDefaultMaxMessageSize};
    std::string type_name{"mw.raw"};
    std::string type_hash{"mw.raw.v1"};
};

struct RegistryConfig {
    std::string socket_path{"/tmp/mw_registry.sock"};
    std::chrono::milliseconds request_timeout{2000};
};

} // namespace mw
