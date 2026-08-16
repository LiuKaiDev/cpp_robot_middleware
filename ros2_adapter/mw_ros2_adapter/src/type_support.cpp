#include "mw_ros2_adapter/type_support.hpp"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace mw_ros2_adapter {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t fnv1a64(const std::string& value) noexcept {
    std::uint64_t hash = kFnvOffsetBasis;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

std::optional<SupportedMessageType> parseMessageType(std::string_view canonical_name) noexcept {
    if (canonical_name == "std_msgs/msg/String") {
        return SupportedMessageType::String;
    }
    if (canonical_name == "geometry_msgs/msg/Twist") {
        return SupportedMessageType::Twist;
    }
    if (canonical_name == "sensor_msgs/msg/Image") {
        return SupportedMessageType::Image;
    }
    return std::nullopt;
}

const char* canonicalTypeName(SupportedMessageType type) noexcept {
    switch (type) {
    case SupportedMessageType::String:
        return "std_msgs/msg/String";
    case SupportedMessageType::Twist:
        return "geometry_msgs/msg/Twist";
    case SupportedMessageType::Image:
        return "sensor_msgs/msg/Image";
    }
    return "unsupported";
}

std::string adapterWireTypeHash(SupportedMessageType type) {
    const std::string identity =
        std::string{"mw_ros2_adapter|ros2-cdr|v1|"} + canonicalTypeName(type);
    std::ostringstream stream;
    stream << "mw_ros2_adapter.cdr.v1.fnv1a64:" << std::hex << std::setfill('0') << std::setw(16)
           << fnv1a64(identity);
    return stream.str();
}

} // namespace mw_ros2_adapter
