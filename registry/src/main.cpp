#include <mw/registry/registry_server.hpp>

#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void handleSignal(int) { stop_requested = 1; }

struct Options {
    std::string socket_path{"/tmp/mw_registry.sock"};
    mw::LivenessConfig liveness;
};

std::chrono::milliseconds parsePositiveMilliseconds(std::string_view value) {
    std::size_t consumed = 0U;
    const long long parsed = std::stoll(std::string{value}, &consumed);
    if (consumed != value.size() || parsed <= 0) {
        throw std::invalid_argument("liveness timing must be a positive integer");
    }
    return std::chrono::milliseconds{parsed};
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            throw std::invalid_argument(
                "usage: mw_registryd [--socket PATH] [--heartbeat-interval-ms N] "
                "[--suspect-timeout-ms N] [--dead-timeout-ms N]");
        }
        const std::string_view option{argv[index]};
        if (option == "--socket") {
            options.socket_path = argv[index + 1];
        } else if (option == "--heartbeat-interval-ms") {
            options.liveness.heartbeat_interval = parsePositiveMilliseconds(argv[index + 1]);
        } else if (option == "--suspect-timeout-ms") {
            options.liveness.suspect_timeout = parsePositiveMilliseconds(argv[index + 1]);
        } else if (option == "--dead-timeout-ms") {
            options.liveness.dead_timeout = parsePositiveMilliseconds(argv[index + 1]);
        } else {
            throw std::invalid_argument("unknown mw_registryd option");
        }
    }
    if (!mw::validLivenessConfig(options.liveness)) {
        throw std::invalid_argument(
            "heartbeat_interval must be less than suspect_timeout and dead_timeout");
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        mw::registry::RegistryServer server{options.socket_path, options.liveness};
        std::cout << "registry_socket=" << server.socketPath() << '\n';
        std::cout.flush();
        while (stop_requested == 0) {
            server.pollOnce(250);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "registry error: " << error.what() << '\n';
        return 1;
    }
}
