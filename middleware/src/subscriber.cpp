#include <mw/subscriber.hpp>

#include <mw/config.hpp>

#include "detail/frame_protocol.hpp"
#include "detail/registry_client.hpp"
#include "detail/unique_fd.hpp"
#include "detail/unix_socket.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <utility>
#include <vector>

namespace mw {
namespace {

void validateConfig(const SubscriberConfig& config, bool registry_mode) {
    if (config.socket_path.empty()) {
        throw std::invalid_argument("subscriber socket path must not be empty");
    }
    if (config.max_message_size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("subscriber max_message_size exceeds the frame protocol");
    }
    if (registry_mode && (config.type_name.empty() || config.type_hash.empty())) {
        throw std::invalid_argument("subscriber type name and hash must not be empty");
    }
}

enum class ReceiveStatus {
    NeedMore,
    MessageReady,
    Disconnected,
    Truncated,
    InvalidFrame,
    MessageTooLarge,
    IoError,
};

struct ReceiveResult {
    ReceiveStatus status{ReceiveStatus::NeedMore};
    std::optional<ReceivedMessage> message;
};

int pollTimeout(std::chrono::steady_clock::time_point deadline,
                std::chrono::milliseconds requested_timeout) noexcept {
    if (requested_timeout.count() <= 0) {
        return 0;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }

    const auto remaining = deadline - now;
    const auto rounded_up = std::chrono::duration_cast<std::chrono::milliseconds>(
        remaining + std::chrono::milliseconds{1} - std::chrono::nanoseconds{1});
    return static_cast<int>(
        std::min<std::int64_t>(rounded_up.count(), std::numeric_limits<int>::max()));
}

} // namespace

struct Subscriber::Impl {
    Impl(std::string topic_value, const SubscriberConfig& config_value)
        : topic(std::move(topic_value)), config(config_value),
          listener(detail::UnixListener::create(config.socket_path)) {}

    Impl(std::string topic_value, const SubscriberConfig& config_value,
         std::shared_ptr<detail::RegistrySession> registry_session_value)
        : topic(std::move(topic_value)), config(config_value),
          listener(detail::UnixListener::create(config.socket_path)),
          registry_session(std::move(registry_session_value)) {
        endpoint_id = registry_session->subscribe(topic, config).endpoint_id;
    }

    ~Impl() {
        if (registry_session && endpoint_id != 0U) {
            registry_session->unsubscribe(endpoint_id);
        }
    }

    ReceiveResult receiveAvailable() {
        while (true) {
            if (header_bytes < header_buffer.size()) {
                const ssize_t received =
                    ::recv(connection.get(), header_buffer.data() + header_bytes,
                           header_buffer.size() - header_bytes, MSG_DONTWAIT);
                if (received > 0) {
                    header_bytes += static_cast<std::size_t>(received);
                    continue;
                }
                if (received == 0) {
                    return {header_bytes == 0U ? ReceiveStatus::Disconnected
                                               : ReceiveStatus::Truncated,
                            std::nullopt};
                }
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return {ReceiveStatus::NeedMore, std::nullopt};
                }
                return {ReceiveStatus::IoError, std::nullopt};
            }

            if (!decoded_header.has_value()) {
                decoded_header =
                    detail::decodeFrameHeader(header_buffer.data(), header_buffer.size());
                if (!decoded_header.has_value()) {
                    return {ReceiveStatus::InvalidFrame, std::nullopt};
                }

                const detail::FrameValidation validation =
                    detail::validateFrameHeader(*decoded_header, config.max_message_size);
                if (validation == detail::FrameValidation::BadMagic) {
                    return {ReceiveStatus::InvalidFrame, std::nullopt};
                }
                if (validation == detail::FrameValidation::PayloadTooLarge) {
                    return {ReceiveStatus::MessageTooLarge, std::nullopt};
                }
                payload.resize(decoded_header->payload_size);
            }

            if (payload_bytes < payload.size()) {
                const ssize_t received = ::recv(connection.get(), payload.data() + payload_bytes,
                                                payload.size() - payload_bytes, MSG_DONTWAIT);
                if (received > 0) {
                    payload_bytes += static_cast<std::size_t>(received);
                    continue;
                }
                if (received == 0) {
                    return {ReceiveStatus::Truncated, std::nullopt};
                }
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return {ReceiveStatus::NeedMore, std::nullopt};
                }
                return {ReceiveStatus::IoError, std::nullopt};
            }

            ReceivedMessage message{std::move(payload), decoded_header->sequence,
                                    decoded_header->timestamp_ns};
            resetFrame();
            return {ReceiveStatus::MessageReady, std::move(message)};
        }
    }

    void resetConnection() noexcept {
        connection.reset();
        resetFrame();
    }

    void resetFrame() noexcept {
        header_bytes = 0;
        decoded_header.reset();
        payload.clear();
        payload_bytes = 0;
    }

    std::string topic;
    SubscriberConfig config;
    detail::UnixListener listener;
    detail::UniqueFd connection;
    std::array<std::uint8_t, detail::kFrameHeaderSize> header_buffer{};
    std::size_t header_bytes{0};
    std::optional<detail::FrameHeader> decoded_header;
    std::vector<std::uint8_t> payload;
    std::size_t payload_bytes{0};
    ErrorCode last_error{ErrorCode::Ok};
    std::shared_ptr<detail::RegistrySession> registry_session;
    std::uint64_t endpoint_id{0};
};

Subscriber::Subscriber(std::string topic, const SubscriberConfig& config) {
    validateConfig(config, false);
    impl_ = std::make_unique<Impl>(std::move(topic), config);
}

Subscriber::Subscriber(std::string topic, const SubscriberConfig& config,
                       std::shared_ptr<detail::RegistrySession> registry_session) {
    validateConfig(config, true);
    impl_ = std::make_unique<Impl>(std::move(topic), config, std::move(registry_session));
}

Subscriber::~Subscriber() = default;
Subscriber::Subscriber(Subscriber&&) noexcept = default;
Subscriber& Subscriber::operator=(Subscriber&&) noexcept = default;

std::optional<ReceivedMessage> Subscriber::take() {
    return waitAndTake(std::chrono::milliseconds{0});
}

std::optional<ReceivedMessage> Subscriber::waitAndTake(std::chrono::milliseconds timeout) {
    if (!impl_) {
        return std::nullopt;
    }

    timeout = std::max(timeout, std::chrono::milliseconds{0});
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        if (!impl_->connection) {
            pollfd descriptor{impl_->listener.fd(), POLLIN, 0};
            const int result = ::poll(&descriptor, 1, pollTimeout(deadline, timeout));
            if (result == 0) {
                impl_->last_error = ErrorCode::Timeout;
                return std::nullopt;
            }
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                impl_->last_error = ErrorCode::IoError;
                return std::nullopt;
            }
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                impl_->last_error = ErrorCode::IoError;
                return std::nullopt;
            }

            try {
                auto accepted = impl_->listener.accept();
                if (!accepted.has_value()) {
                    continue;
                }
                impl_->connection = std::move(*accepted);
                impl_->resetFrame();
            } catch (const std::system_error&) {
                impl_->last_error = ErrorCode::IoError;
                return std::nullopt;
            }
            continue;
        }

        pollfd descriptor{impl_->connection.get(), POLLIN, 0};
        const int result = ::poll(&descriptor, 1, pollTimeout(deadline, timeout));
        if (result == 0) {
            impl_->last_error = ErrorCode::Timeout;
            return std::nullopt;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            impl_->last_error = ErrorCode::IoError;
            impl_->resetConnection();
            return std::nullopt;
        }
        if ((descriptor.revents & POLLNVAL) != 0) {
            impl_->last_error = ErrorCode::IoError;
            impl_->resetConnection();
            return std::nullopt;
        }
        if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
            continue;
        }

        ReceiveResult receive_result = impl_->receiveAvailable();
        switch (receive_result.status) {
        case ReceiveStatus::NeedMore:
            continue;
        case ReceiveStatus::MessageReady:
            impl_->last_error = ErrorCode::Ok;
            return std::move(receive_result.message);
        case ReceiveStatus::Disconnected:
            impl_->last_error = ErrorCode::ConnectionLost;
            impl_->resetConnection();
            return std::nullopt;
        case ReceiveStatus::Truncated:
        case ReceiveStatus::InvalidFrame:
            impl_->last_error = ErrorCode::InvalidFrame;
            impl_->resetConnection();
            return std::nullopt;
        case ReceiveStatus::MessageTooLarge:
            impl_->last_error = ErrorCode::MessageTooLarge;
            impl_->resetConnection();
            return std::nullopt;
        case ReceiveStatus::IoError:
            impl_->last_error = ErrorCode::IoError;
            impl_->resetConnection();
            return std::nullopt;
        }
    }
}

ErrorCode Subscriber::lastError() const noexcept {
    return impl_ ? impl_->last_error : ErrorCode::InvalidState;
}

const std::string& Subscriber::topic() const noexcept {
    static const std::string empty;
    return impl_ ? impl_->topic : empty;
}

} // namespace mw
