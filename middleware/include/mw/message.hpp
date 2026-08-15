#pragma once

#include <cstdint>
#include <vector>

namespace mw {

struct ReceivedMessage {
    std::vector<std::uint8_t> payload;
    std::uint64_t sequence{0};
    std::uint64_t publish_timestamp_ns{0};
};

} // namespace mw
