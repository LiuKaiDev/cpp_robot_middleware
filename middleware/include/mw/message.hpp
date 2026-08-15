#pragma once

#include <cstdint>
#include <vector>

namespace mw {

struct ReceivedMessage {
    std::vector<std::uint8_t> payload;
    std::uint64_t sequence{0};
    std::uint64_t publish_timestamp_ns{0};
    std::uint64_t pool_id{0};
    std::uint32_t chunk_index{0};
    std::uint32_t chunk_generation{0};
    std::uint64_t payload_offset{0};
};

} // namespace mw
