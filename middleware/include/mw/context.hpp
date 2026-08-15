#pragma once

#include <mw/config.hpp>
#include <mw/publisher.hpp>
#include <mw/subscriber.hpp>

#include <memory>
#include <string>

namespace mw {

class Context {
  public:
    explicit Context(std::string node_name);
    Context(std::string node_name, const RegistryConfig& registry_config);

    Publisher createPublisher(const std::string& topic, const PublisherConfig& config) const;
    Subscriber createSubscriber(const std::string& topic, const SubscriberConfig& config) const;

    const std::string& nodeName() const noexcept;

  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace mw
