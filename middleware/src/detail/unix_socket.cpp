#include "detail/unix_socket.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>

namespace mw::detail {
namespace {

sockaddr_un makeAddress(const std::string& socket_path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;

    if (socket_path.empty()) {
        throw std::invalid_argument("Unix socket path must not be empty");
    }
    if (socket_path.size() >= sizeof(address.sun_path)) {
        throw std::invalid_argument("Unix socket path is too long");
    }

    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1U);
    return address;
}

UniqueFd createSocket(int flags) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | flags, 0);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "socket(AF_UNIX)");
    }
    return UniqueFd{fd};
}

} // namespace

UnixListener UnixListener::create(const std::string& socket_path, int backlog) {
    const sockaddr_un address = makeAddress(socket_path);
    UniqueFd fd = createSocket(SOCK_NONBLOCK);

    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::system_error(errno, std::generic_category(), "bind(" + socket_path + ")");
    }

    if (backlog <= 0) {
        ::unlink(socket_path.c_str());
        throw std::invalid_argument("Unix socket listen backlog must be positive");
    }

    if (::listen(fd.get(), backlog) < 0) {
        const int error_number = errno;
        ::unlink(socket_path.c_str());
        throw std::system_error(error_number, std::generic_category(),
                                "listen(" + socket_path + ")");
    }

    return UnixListener{std::move(fd), socket_path};
}

UnixListener::UnixListener(UniqueFd fd, std::string socket_path) noexcept
    : fd_(std::move(fd)), socket_path_(std::move(socket_path)), owns_path_(true) {}

UnixListener::~UnixListener() { cleanup(); }

UnixListener::UnixListener(UnixListener&& other) noexcept
    : fd_(std::move(other.fd_)), socket_path_(std::move(other.socket_path_)),
      owns_path_(other.owns_path_) {
    other.owns_path_ = false;
}

UnixListener& UnixListener::operator=(UnixListener&& other) noexcept {
    if (this != &other) {
        cleanup();
        fd_ = std::move(other.fd_);
        socket_path_ = std::move(other.socket_path_);
        owns_path_ = other.owns_path_;
        other.owns_path_ = false;
    }
    return *this;
}

std::optional<UniqueFd> UnixListener::accept() {
    const int accepted = ::accept4(fd_.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (accepted >= 0) {
        return UniqueFd{accepted};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return std::nullopt;
    }
    if (errno == EINTR) {
        return std::nullopt;
    }
    throw std::system_error(errno, std::generic_category(), "accept4(AF_UNIX)");
}

void UnixListener::cleanup() noexcept {
    fd_.reset();
    if (owns_path_) {
        ::unlink(socket_path_.c_str());
        owns_path_ = false;
    }
}

UniqueFd connectUnixSocket(const std::string& socket_path) {
    const sockaddr_un address = makeAddress(socket_path);
    UniqueFd fd = createSocket(0);
    if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::system_error(errno, std::generic_category(), "connect(" + socket_path + ")");
    }
    return fd;
}

} // namespace mw::detail
