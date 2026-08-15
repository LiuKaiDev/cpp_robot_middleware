#pragma once

#include <mw/config.hpp>
#include <mw/publisher.hpp>
#include <mw/subscriber.hpp>

#include <string>

namespace mw {

class Context {
  public:
    explicit Context(std::string node_name);

    Publisher createPublisher(const std::string& topic, const PublisherConfig& config) const;
    Subscriber createSubscriber(const std::string& topic, const SubscriberConfig& config) const;

    const std::string& nodeName() const noexcept;

  private:
    std::string node_name_;
};

} // namespace mw
