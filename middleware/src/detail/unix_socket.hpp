#pragma once

#include "detail/unique_fd.hpp"

#include <optional>
#include <string>

namespace mw::detail {

class UnixListener {
  public:
    static UnixListener create(const std::string& socket_path);

    ~UnixListener();

    UnixListener(const UnixListener&) = delete;
    UnixListener& operator=(const UnixListener&) = delete;
    UnixListener(UnixListener&& other) noexcept;
    UnixListener& operator=(UnixListener&& other) noexcept;

    int fd() const noexcept { return fd_.get(); }
    std::optional<UniqueFd> accept();

  private:
    UnixListener(UniqueFd fd, std::string socket_path) noexcept;
    void cleanup() noexcept;

    UniqueFd fd_;
    std::string socket_path_;
    bool owns_path_{false};
};

UniqueFd connectUnixSocket(const std::string& socket_path);

} // namespace mw::detail
