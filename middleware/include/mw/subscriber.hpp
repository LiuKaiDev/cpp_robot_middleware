#pragma once

#include <mw/message.hpp>
#include <mw/result.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace mw {

struct SubscriberConfig;

class Subscriber {
  public:
    ~Subscriber();

    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    Subscriber(Subscriber&&) noexcept;
    Subscriber& operator=(Subscriber&&) noexcept;

    std::optional<ReceivedMessage> take();
    std::optional<ReceivedMessage> waitAndTake(std::chrono::milliseconds timeout);

    ErrorCode lastError() const noexcept;
    const std::string& topic() const noexcept;

  private:
    friend class Context;
    struct Impl;

    Subscriber(std::string topic, const SubscriberConfig& config);

    std::unique_ptr<Impl> impl_;
};

} // namespace mw
