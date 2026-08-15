#include "detail/socket_io.hpp"

#include <cerrno>
#include <sys/socket.h>

namespace mw::detail {

namespace {

bool isDisconnectError(int error_number) noexcept {
    return error_number == EPIPE || error_number == ECONNRESET || error_number == ENOTCONN;
}

} // namespace

IoResult readExact(int fd, void* buffer, std::size_t size) noexcept {
    IoResult result = readExactWith(buffer, size, [fd](void* destination, std::size_t remaining) {
        return ::recv(fd, destination, remaining, 0);
    });
    if (result.status == IoStatus::Error && isDisconnectError(result.error_number)) {
        result.status = IoStatus::Closed;
    }
    return result;
}

IoResult writeAll(int fd, const void* buffer, std::size_t size) noexcept {
    IoResult result = writeAllWith(buffer, size, [fd](const void* source, std::size_t remaining) {
        return ::send(fd, source, remaining, MSG_NOSIGNAL);
    });
    if (result.status == IoStatus::Error && isDisconnectError(result.error_number)) {
        result.status = IoStatus::Closed;
    }
    return result;
}

} // namespace mw::detail
