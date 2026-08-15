#pragma once

#include <mw/message.hpp>
#include <mw/result.hpp>
#include <mw/sample_view.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace mw {

struct SubscriberConfig;
namespace detail {
class RegistrySession;
}

class Subscriber {
  public:
    ~Subscriber();

    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    Subscriber(Subscriber&&) noexcept;
    Subscriber& operator=(Subscriber&&) noexcept;

    std::optional<ReceivedMessage> take();
    std::optional<ReceivedMessage> waitAndTake(std::chrono::milliseconds timeout);
    std::optional<SampleView> takeView();
    std::optional<SampleView> waitAndTakeView(std::chrono::milliseconds timeout);

    ErrorCode lastError() const noexcept;
    const std::string& topic() const noexcept;

  private:
    friend class Context;
    struct Impl;

    Subscriber(std::string topic, const SubscriberConfig& config);
    Subscriber(std::string topic, const SubscriberConfig& config,
               std::shared_ptr<detail::RegistrySession> registry_session);

    std::unique_ptr<Impl> impl_;
};

} // namespace mw
