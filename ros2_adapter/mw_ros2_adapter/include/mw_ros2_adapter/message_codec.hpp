#pragma once

#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace mw_ros2_adapter {

class MessageCodec {
  public:
    template <typename MessageT>
    static std::vector<std::uint8_t> serialize(const MessageT& message) {
        rclcpp::Serialization<MessageT> serialization;
        rclcpp::SerializedMessage serialized;
        serialization.serialize_message(&message, &serialized);

        const auto& raw = serialized.get_rcl_serialized_message();
        if (raw.buffer_length == 0U) {
            return {};
        }
        if (raw.buffer == nullptr) {
            throw std::runtime_error("ROS serialization returned a null buffer");
        }
        return std::vector<std::uint8_t>(raw.buffer, raw.buffer + raw.buffer_length);
    }

    template <typename MessageT> static MessageT deserialize(const void* data, std::size_t size) {
        if (size == 0U) {
            throw std::invalid_argument("serialized ROS message is empty");
        }
        if (data == nullptr) {
            throw std::invalid_argument("serialized ROS message data is null");
        }

        rclcpp::SerializedMessage serialized(size);
        auto& raw = serialized.get_rcl_serialized_message();
        std::memcpy(raw.buffer, data, size);
        raw.buffer_length = size;

        rclcpp::Serialization<MessageT> serialization;
        MessageT message;
        serialization.deserialize_message(&serialized, &message);
        return message;
    }

    template <typename MessageT>
    static MessageT deserialize(const std::vector<std::uint8_t>& bytes) {
        return deserialize<MessageT>(bytes.data(), bytes.size());
    }
};

} // namespace mw_ros2_adapter
