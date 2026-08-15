#pragma once

#include <memory>
#include <string>

namespace mw::registry {

class RegistryServer {
  public:
    explicit RegistryServer(std::string socket_path);
    ~RegistryServer();

    RegistryServer(const RegistryServer&) = delete;
    RegistryServer& operator=(const RegistryServer&) = delete;

    void pollOnce(int timeout_ms);
    const std::string& socketPath() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mw::registry
