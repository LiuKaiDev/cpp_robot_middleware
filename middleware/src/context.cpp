#include <mw/context.hpp>

#include <stdexcept>
#include <utility>

namespace mw {

Context::Context(std::string node_name) : node_name_(std::move(node_name)) {
    if (node_name_.empty()) {
        throw std::invalid_argument("node name must not be empty");
    }
}

Publisher Context::createPublisher(const std::string& topic, const PublisherConfig& config) const {
    if (topic.empty()) {
        throw std::invalid_argument("topic must not be empty");
    }
    return Publisher{topic, config};
}

Subscriber Context::createSubscriber(const std::string& topic,
                                     const SubscriberConfig& config) const {
    if (topic.empty()) {
        throw std::invalid_argument("topic must not be empty");
    }
    return Subscriber{topic, config};
}

const std::string& Context::nodeName() const noexcept { return node_name_; }

} // namespace mw
