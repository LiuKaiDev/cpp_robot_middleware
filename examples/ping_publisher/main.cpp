#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/result.hpp>

#include "ping_common.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <vector>

int main(int argc, char** argv) {
    try {
        const mw::examples::PingOptions options = mw::examples::parseOptions(argc, argv);
        if (options.payload_size > std::numeric_limits<std::uint32_t>::max()) {
            std::cerr << "payload size exceeds the frame protocol\n";
            return 2;
        }

        mw::Context context =
            options.registry_path.empty()
                ? mw::Context{"ping_publisher"}
                : mw::Context{"ping_publisher", mw::RegistryConfig{options.registry_path}};
        mw::PublisherConfig config;
        config.socket_path = options.socket_path;
        config.max_message_size = std::max(mw::kDefaultMaxMessageSize, options.payload_size);
        config.type_name = options.type_name;
        config.type_hash = options.type_hash;
        auto publisher = context.createPublisher(options.topic, config);

        std::vector<std::uint8_t> payload(options.payload_size);
        std::size_t sent = 0;
        std::size_t publish_errors = 0;

        for (std::size_t index = 0; index < options.count; ++index) {
            const std::uint64_t expected_sequence = static_cast<std::uint64_t>(index) + 1U;
            mw::examples::fillPayload(payload, expected_sequence);
            const mw::PublishResult result = publisher.publish(payload.data(), payload.size());
            if (!result || result.sequence != expected_sequence) {
                ++publish_errors;
                std::cerr << "publish failed at sequence=" << expected_sequence
                          << " error=" << mw::errorMessage(result.error) << '\n';
                break;
            }
            ++sent;
        }

        std::cout << "sent=" << sent << " publish_errors=" << publish_errors << '\n';
        return sent == options.count && publish_errors == 0U ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "publisher error: " << error.what() << '\n';
        return 2;
    }
}
