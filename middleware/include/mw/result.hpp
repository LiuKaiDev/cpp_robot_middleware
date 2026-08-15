#pragma once

#include <cstddef>
#include <cstdint>

namespace mw {

enum class ErrorCode {
    Ok,
    InvalidArgument,
    InvalidState,
    MessageTooLarge,
    ConnectionLost,
    IoError,
    Timeout,
    InvalidFrame,
};

const char* errorMessage(ErrorCode error) noexcept;

struct PublishResult {
    ErrorCode error{ErrorCode::Ok};
    std::uint64_t sequence{0};
    std::size_t payload_size{0};

    bool ok() const noexcept { return error == ErrorCode::Ok; }
    explicit operator bool() const noexcept { return ok(); }
};

} // namespace mw
