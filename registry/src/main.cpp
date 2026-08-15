#include <mw/registry/registry_server.hpp>

#include <csignal>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void handleSignal(int) { stop_requested = 1; }

std::string parseSocketPath(int argc, char** argv) {
    std::string socket_path{"/tmp/mw_registry.sock"};
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc || std::string_view{argv[index]} != "--socket") {
            throw std::invalid_argument("usage: mw_registryd [--socket PATH]");
        }
        socket_path = argv[index + 1];
    }
    return socket_path;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string socket_path = parseSocketPath(argc, argv);
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        mw::registry::RegistryServer server{socket_path};
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
