#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace mw {

enum class ErrorCode : std::uint16_t {
    Ok = 0,
    InvalidArgument = 1,
    InvalidState = 2,
    MessageTooLarge = 3,
    ConnectionLost = 4,
    IoError = 5,
    Timeout = 6,
    InvalidFrame = 7,
    RegistryUnavailable = 8,
    DuplicateNode = 9,
    DuplicatePublisher = 10,
    TypeMismatch = 11,
    TopicNotFound = 12,
    InvalidControlMessage = 13,
    UnsupportedProtocolVersion = 14,
    UnknownOpcode = 15,
    InvalidRequestId = 16,
    NotRegistered = 17,
    EndpointNotFound = 18,
};

const char* errorMessage(ErrorCode error) noexcept;

class MiddlewareError : public std::runtime_error {
  public:
    MiddlewareError(ErrorCode code, std::string message);

    ErrorCode code() const noexcept { return code_; }

  private:
    ErrorCode code_;
};

struct PublishResult {
    ErrorCode error{ErrorCode::Ok};
    std::uint64_t sequence{0};
    std::size_t payload_size{0};

    bool ok() const noexcept { return error == ErrorCode::Ok; }
    explicit operator bool() const noexcept { return ok(); }
};

} // namespace mw
