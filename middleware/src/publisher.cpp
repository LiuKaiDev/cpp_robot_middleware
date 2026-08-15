#include <mw/publisher.hpp>

#include <mw/config.hpp>

#include "detail/frame_protocol.hpp"
#include "detail/socket_io.hpp"
#include "detail/unix_socket.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mw {
namespace {

void validateConfig(const PublisherConfig& config) {
    if (config.socket_path.empty()) {
        throw std::invalid_argument("publisher socket path must not be empty");
    }
    if (config.max_message_size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("publisher max_message_size exceeds the frame protocol");
    }
}

std::uint64_t monotonicTimestampNs() noexcept {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp).count());
}

ErrorCode mapIoError(detail::IoStatus status) noexcept {
    return status == detail::IoStatus::Closed ? ErrorCode::ConnectionLost : ErrorCode::IoError;
}

} // namespace

struct Publisher::Impl {
    Impl(std::string topic_value, const PublisherConfig& config_value)
        : topic(std::move(topic_value)), config(config_value),
          socket(detail::connectUnixSocket(config.socket_path)) {}

    std::string topic;
    PublisherConfig config;
    detail::UniqueFd socket;
    std::uint64_t sequence{0};
};

Publisher::Publisher(std::string topic, const PublisherConfig& config) {
    validateConfig(config);
    impl_ = std::make_unique<Impl>(std::move(topic), config);
}

Publisher::~Publisher() = default;
Publisher::Publisher(Publisher&&) noexcept = default;
Publisher& Publisher::operator=(Publisher&&) noexcept = default;

PublishResult Publisher::publish(const void* data, std::size_t size) {
    if (!impl_ || !impl_->socket) {
        return {ErrorCode::InvalidState, 0, 0};
    }
    if (data == nullptr && size != 0U) {
        return {ErrorCode::InvalidArgument, 0, 0};
    }
    if (size > impl_->config.max_message_size || size > std::numeric_limits<std::uint32_t>::max()) {
        return {ErrorCode::MessageTooLarge, 0, 0};
    }
    if (impl_->sequence == std::numeric_limits<std::uint64_t>::max()) {
        return {ErrorCode::InvalidState, 0, 0};
    }

    const std::uint64_t next_sequence = impl_->sequence + 1U;
    const detail::FrameHeader header{
        detail::kFrameMagic,
        static_cast<std::uint32_t>(size),
        next_sequence,
        monotonicTimestampNs(),
    };
    const auto encoded_header = detail::encodeFrameHeader(header);

    const detail::IoResult header_result =
        detail::writeAll(impl_->socket.get(), encoded_header.data(), encoded_header.size());
    if (header_result.status != detail::IoStatus::Complete) {
        const ErrorCode error = mapIoError(header_result.status);
        impl_->socket.reset();
        return {error, next_sequence, 0};
    }

    const detail::IoResult payload_result = detail::writeAll(impl_->socket.get(), data, size);
    if (payload_result.status != detail::IoStatus::Complete) {
        const ErrorCode error = mapIoError(payload_result.status);
        impl_->socket.reset();
        return {error, next_sequence, 0};
    }

    impl_->sequence = next_sequence;
    return {ErrorCode::Ok, next_sequence, size};
}

const std::string& Publisher::topic() const noexcept {
    static const std::string empty;
    return impl_ ? impl_->topic : empty;
}

const char* errorMessage(ErrorCode error) noexcept {
    switch (error) {
    case ErrorCode::Ok:
        return "ok";
    case ErrorCode::InvalidArgument:
        return "invalid argument";
    case ErrorCode::InvalidState:
        return "invalid state";
    case ErrorCode::MessageTooLarge:
        return "message too large";
    case ErrorCode::ConnectionLost:
        return "connection lost";
    case ErrorCode::IoError:
        return "I/O error";
    case ErrorCode::Timeout:
        return "timeout";
    case ErrorCode::InvalidFrame:
        return "invalid frame";
    }
    return "unknown error";
}

} // namespace mw
