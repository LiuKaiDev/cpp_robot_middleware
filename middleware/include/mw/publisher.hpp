#pragma once

#include <mw/result.hpp>

#include <cstddef>
#include <memory>
#include <string>

namespace mw {

struct PublisherConfig;
namespace detail {
class RegistrySession;
}

class Publisher {
  public:
    ~Publisher();

    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;
    Publisher(Publisher&&) noexcept;
    Publisher& operator=(Publisher&&) noexcept;

    PublishResult publish(const void* data, std::size_t size);
    const std::string& topic() const noexcept;

  private:
    friend class Context;
    struct Impl;

    Publisher(std::string topic, const PublisherConfig& config);
    Publisher(std::string topic, const PublisherConfig& config,
              std::shared_ptr<detail::RegistrySession> registry_session);

    std::unique_ptr<Impl> impl_;
};

} // namespace mw
