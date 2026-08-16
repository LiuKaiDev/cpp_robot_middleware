#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mw_ros2_adapter {

enum class SupportedMessageType {
    String,
    Twist,
    Image,
};

std::optional<SupportedMessageType> parseMessageType(std::string_view canonical_name) noexcept;
const char* canonicalTypeName(SupportedMessageType type) noexcept;
std::string adapterWireTypeHash(SupportedMessageType type);

} // namespace mw_ros2_adapter
