#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace mw::detail {

enum class IoStatus {
    Complete,
    Closed,
    Error,
};

struct IoResult {
    IoStatus status{IoStatus::Complete};
    std::size_t transferred{0};
    int error_number{0};
};

template <typename ReadOperation>
IoResult readExactWith(void* buffer, std::size_t size, ReadOperation&& operation) {
    auto* current = static_cast<std::uint8_t*>(buffer);
    std::size_t transferred = 0;

    while (transferred < size) {
        const std::size_t remaining = size - transferred;
        const ssize_t result = operation(current + transferred, remaining);
        if (result > 0) {
            if (static_cast<std::size_t>(result) > remaining) {
                return {IoStatus::Error, transferred, EIO};
            }
            transferred += static_cast<std::size_t>(result);
            continue;
        }
        if (result == 0) {
            return {IoStatus::Closed, transferred, 0};
        }
        if (errno == EINTR) {
            continue;
        }
        return {IoStatus::Error, transferred, errno};
    }

    return {IoStatus::Complete, transferred, 0};
}

template <typename WriteOperation>
IoResult writeAllWith(const void* buffer, std::size_t size, WriteOperation&& operation) {
    const auto* current = static_cast<const std::uint8_t*>(buffer);
    std::size_t transferred = 0;

    while (transferred < size) {
        const std::size_t remaining = size - transferred;
        const ssize_t result = operation(current + transferred, remaining);
        if (result > 0) {
            if (static_cast<std::size_t>(result) > remaining) {
                return {IoStatus::Error, transferred, EIO};
            }
            transferred += static_cast<std::size_t>(result);
            continue;
        }
        if (result == 0) {
            return {IoStatus::Closed, transferred, 0};
        }
        if (errno == EINTR) {
            continue;
        }
        return {IoStatus::Error, transferred, errno};
    }

    return {IoStatus::Complete, transferred, 0};
}

IoResult readExact(int fd, void* buffer, std::size_t size) noexcept;
IoResult writeAll(int fd, const void* buffer, std::size_t size) noexcept;

} // namespace mw::detail
