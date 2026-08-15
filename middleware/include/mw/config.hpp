#pragma once

#include <cstddef>
#include <string>

namespace mw {

inline constexpr std::size_t kDefaultMaxMessageSize = 4U * 1024U * 1024U;

struct PublisherConfig {
    std::string socket_path;
    std::size_t max_message_size{kDefaultMaxMessageSize};
};

struct SubscriberConfig {
    std::string socket_path;
    std::size_t max_message_size{kDefaultMaxMessageSize};
};

} // namespace mw
