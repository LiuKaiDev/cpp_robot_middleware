#pragma once

#include <mw/config.hpp>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mw::examples {

struct PingOptions {
    std::string registry_path;
    std::string socket_path{"/tmp/cpp_robot_middleware_ping.sock"};
    std::string topic{"/ping"};
    std::string type_name{"mw.examples.Ping"};
    std::string type_hash{"mw.examples.Ping.v1"};
    std::size_t count{1};
    std::size_t payload_size{64};
    std::chrono::milliseconds timeout{5000};
    mw::TransportType transport{mw::TransportType::UnixDomainSocket};
};

inline std::size_t parseSize(std::string_view text, std::string_view option_name) {
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid value for " + std::string(option_name));
    }
    return value;
}

inline PingOptions parseOptions(int argc, char** argv) {
    PingOptions options;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argv[index]));
        }

        const std::string_view name{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (name == "--registry") {
            options.registry_path = value;
        } else if (name == "--socket") {
            options.socket_path = value;
        } else if (name == "--topic") {
            options.topic = value;
        } else if (name == "--type-name") {
            options.type_name = value;
        } else if (name == "--type-hash") {
            options.type_hash = value;
        } else if (name == "--count") {
            options.count = parseSize(value, name);
        } else if (name == "--size") {
            options.payload_size = parseSize(value, name);
        } else if (name == "--timeout-ms") {
            const std::size_t timeout = parseSize(value, name);
            options.timeout = std::chrono::milliseconds{timeout};
        } else if (name == "--transport") {
            if (value == "uds") {
                options.transport = mw::TransportType::UnixDomainSocket;
            } else if (value == "shm") {
                options.transport = mw::TransportType::SharedMemory;
            } else {
                throw std::invalid_argument("--transport must be uds or shm");
            }
        } else {
            throw std::invalid_argument("unknown option " + std::string(name));
        }
    }

    if (options.socket_path.empty()) {
        throw std::invalid_argument("--socket must not be empty");
    }
    if (options.topic.empty() || options.type_name.empty() || options.type_hash.empty()) {
        throw std::invalid_argument("topic and type values must not be empty");
    }
    if (options.count == 0U) {
        throw std::invalid_argument("--count must be greater than zero");
    }
    if (options.registry_path.empty() && options.transport == mw::TransportType::SharedMemory) {
        throw std::invalid_argument("--transport shm requires --registry");
    }
    return options;
}

inline std::uint8_t payloadByte(std::uint64_t sequence, std::size_t offset) noexcept {
    return static_cast<std::uint8_t>((sequence + (offset * 31U)) & 0xFFU);
}

inline void fillPayload(std::vector<std::uint8_t>& payload, std::uint64_t sequence) {
    for (std::size_t offset = 0; offset < payload.size(); ++offset) {
        payload[offset] = payloadByte(sequence, offset);
    }
}

inline bool validatePayload(const std::vector<std::uint8_t>& payload,
                            std::uint64_t expected_sequence) noexcept {
    for (std::size_t offset = 0; offset < payload.size(); ++offset) {
        if (payload[offset] != payloadByte(expected_sequence, offset)) {
            return false;
        }
    }
    return true;
}

} // namespace mw::examples
