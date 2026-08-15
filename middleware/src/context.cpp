#include <mw/context.hpp>

#include "detail/registry_client.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace mw {

struct Context::Impl {
    explicit Impl(std::string node_name_value) : node_name(std::move(node_name_value)) {}

    Impl(std::string node_name_value, const RegistryConfig& registry_config)
        : node_name(std::move(node_name_value)),
          registry_session(std::make_shared<detail::RegistrySession>(node_name, registry_config)) {}

    std::string node_name;
    std::shared_ptr<detail::RegistrySession> registry_session;
};

Context::Context(std::string node_name) {
    if (node_name.empty()) {
        throw std::invalid_argument("node name must not be empty");
    }
    impl_ = std::make_shared<Impl>(std::move(node_name));
}

Context::Context(std::string node_name, const RegistryConfig& registry_config) {
    if (node_name.empty()) {
        throw std::invalid_argument("node name must not be empty");
    }
    impl_ = std::make_shared<Impl>(std::move(node_name), registry_config);
}

Publisher Context::createPublisher(const std::string& topic, const PublisherConfig& config) const {
    if (!impl_) {
        throw std::logic_error("context is not initialized");
    }
    if (topic.empty()) {
        throw std::invalid_argument("topic must not be empty");
    }
    if (impl_->registry_session) {
        return Publisher{topic, config, impl_->registry_session};
    }
    return Publisher{topic, config};
}

Subscriber Context::createSubscriber(const std::string& topic,
                                     const SubscriberConfig& config) const {
    if (!impl_) {
        throw std::logic_error("context is not initialized");
    }
    if (topic.empty()) {
        throw std::invalid_argument("topic must not be empty");
    }
    if (impl_->registry_session) {
        return Subscriber{topic, config, impl_->registry_session};
    }
    return Subscriber{topic, config};
}

const std::string& Context::nodeName() const noexcept {
    static const std::string empty;
    return impl_ ? impl_->node_name : empty;
}

} // namespace mw
