#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/result.hpp>

#include "ping_common.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>

int main(int argc, char** argv) {
    try {
        const mw::examples::PingOptions options = mw::examples::parseOptions(argc, argv);
        if (options.payload_size > std::numeric_limits<std::uint32_t>::max()) {
            std::cerr << "payload size exceeds the frame protocol\n";
            return 2;
        }

        mw::Context context =
            options.registry_path.empty()
                ? mw::Context{"ping_subscriber"}
                : mw::Context{"ping_subscriber", mw::RegistryConfig{options.registry_path}};
        mw::SubscriberConfig config;
        config.socket_path = options.socket_path;
        config.max_message_size = std::max(mw::kDefaultMaxMessageSize, options.payload_size);
        config.type_name = options.type_name;
        config.type_hash = options.type_hash;
        config.transport = options.transport;
        auto subscriber = context.createSubscriber(options.topic, config);

        std::size_t received = 0;
        std::size_t sequence_errors = 0;
        std::size_t payload_errors = 0;

        for (std::size_t index = 0; index < options.count; ++index) {
            auto message = subscriber.waitAndTake(options.timeout);
            if (!message.has_value()) {
                std::cerr << "receive failed after " << received
                          << " messages: " << mw::errorMessage(subscriber.lastError()) << '\n';
                break;
            }

            const std::uint64_t expected_sequence = static_cast<std::uint64_t>(index) + 1U;
            if (message->sequence != expected_sequence) {
                ++sequence_errors;
            }
            if (message->payload.size() != options.payload_size ||
                !mw::examples::validatePayload(message->payload, expected_sequence)) {
                ++payload_errors;
            }
            ++received;
        }

        std::cout << "received=" << received << " sequence_errors=" << sequence_errors
                  << " payload_errors=" << payload_errors << '\n';
        return received == options.count && sequence_errors == 0U && payload_errors == 0U ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "subscriber error: " << error.what() << '\n';
        return 2;
    }
}
