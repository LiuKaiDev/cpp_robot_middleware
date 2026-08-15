#include <mw/config.hpp>

#include "detail/registry_client.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#ifndef MW_REGISTRYD_PATH
#error "MW_REGISTRYD_PATH is required"
#endif
#ifndef MW_PING_PUBLISHER_PATH
#error "MW_PING_PUBLISHER_PATH is required"
#endif
#ifndef MW_PING_SUBSCRIBER_PATH
#error "MW_PING_SUBSCRIBER_PATH is required"
#endif

namespace {

using namespace std::chrono_literals;

class ChildProcess {
  public:
    explicit ChildProcess(pid_t process = -1) noexcept : process_(process) {}
    ~ChildProcess() { terminate(); }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept : process_(std::exchange(other.process_, -1)) {}
    ChildProcess& operator=(ChildProcess&& other) noexcept {
        if (this != &other) {
            terminate();
            process_ = std::exchange(other.process_, -1);
        }
        return *this;
    }

    int wait(std::chrono::seconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            int status = 0;
            const pid_t result = ::waitpid(process_, &status, WNOHANG);
            if (result == process_) {
                process_ = -1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
            if (result < 0) {
                process_ = -1;
                return 255;
            }
            std::this_thread::sleep_for(10ms);
        }
        return 254;
    }

  private:
    void terminate() noexcept {
        if (process_ <= 0) {
            return;
        }
        (void)::kill(process_, SIGTERM);
        (void)::waitpid(process_, nullptr, 0);
        process_ = -1;
    }

    pid_t process_{-1};
};

ChildProcess spawn(const std::vector<std::string>& arguments) {
    const pid_t child = ::fork();
    if (child < 0) {
        throw std::system_error(errno, std::generic_category(), "fork");
    }
    if (child == 0) {
        const int null_fd = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_fd >= 0) {
            (void)::dup2(null_fd, STDOUT_FILENO);
            (void)::dup2(null_fd, STDERR_FILENO);
            (void)::close(null_fd);
        }
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1U);
        for (const std::string& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        ::execv(argv.front(), argv.data());
        ::_exit(127);
    }
    return ChildProcess{child};
}

bool waitForRegistry(const std::string& registry_path) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            mw::detail::RegistryClient client{mw::RegistryConfig{registry_path}};
            (void)client.listNodes();
            return true;
        } catch (...) {
            std::this_thread::sleep_for(20ms);
        }
    }
    return false;
}

bool waitForNode(const std::string& registry_path, const std::string& node_name) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            mw::detail::RegistryClient client{mw::RegistryConfig{registry_path}};
            for (const auto& node : client.listNodes()) {
                if (node.node_name == node_name) {
                    return true;
                }
            }
        } catch (...) {
        }
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

std::set<std::string> projectSharedMemoryObjects() {
    std::set<std::string> objects;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator{"/dev/shm", error}, end;
         !error && iterator != end; iterator.increment(error)) {
        const std::string name = iterator->path().filename().string();
        if (name.rfind("mw_p3_", 0U) == 0U) {
            objects.insert(name);
        }
    }
    return objects;
}

void runTransport(std::size_t payload_size, const std::string& transport) {
    const std::string suffix =
        std::to_string(::getpid()) + "_" + transport + "_" + std::to_string(payload_size);
    const std::string registry_path = "/tmp/mw_p3_registry_" + suffix + ".sock";
    const std::string data_path = "/tmp/mw_p3_data_" + suffix + ".sock";
    ChildProcess registry = spawn({MW_REGISTRYD_PATH, "--socket", registry_path});
    ASSERT_TRUE(waitForRegistry(registry_path));

    ChildProcess subscriber =
        spawn({MW_PING_SUBSCRIBER_PATH, "--registry", registry_path, "--socket", data_path,
               "--transport", transport, "--count", "2", "--size", std::to_string(payload_size),
               "--timeout-ms", "10000"});
    ASSERT_TRUE(waitForNode(registry_path, "ping_subscriber"));
    ChildProcess publisher =
        spawn({MW_PING_PUBLISHER_PATH, "--registry", registry_path, "--socket", data_path,
               "--transport", transport, "--count", "2", "--size", std::to_string(payload_size)});

    EXPECT_EQ(publisher.wait(15s), 0) << transport << " publisher size=" << payload_size;
    EXPECT_EQ(subscriber.wait(15s), 0) << transport << " subscriber size=" << payload_size;
}

TEST(ShmTransportIntegrationTest, UdsAndShmTransferIdenticalDeterministicPayloadMatrix) {
    const auto initial_objects = projectSharedMemoryObjects();
    for (const std::size_t size : {1024U, 64U * 1024U, 1024U * 1024U, 4U * 1024U * 1024U}) {
        SCOPED_TRACE(size);
        runTransport(size, "uds");
        runTransport(size, "shm");
        EXPECT_EQ(projectSharedMemoryObjects(), initial_objects);
    }
}

} // namespace
