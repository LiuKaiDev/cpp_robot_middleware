#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/result.hpp>

#include "detail/registry_client.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#ifndef MW_REGISTRYD_PATH
#error "MW_REGISTRYD_PATH is required"
#endif

namespace {

using namespace std::chrono_literals;

mw::LivenessConfig testLiveness() { return {20ms, 80ms, 180ms}; }

mw::RegistryConfig registryConfig(const std::string& path) {
    return mw::RegistryConfig{path, 1s, testLiveness()};
}

bool writeLine(int fd, const std::string& line) noexcept {
    std::string output = line + '\n';
    std::size_t offset = 0U;
    while (offset < output.size()) {
        const ssize_t count = ::write(fd, output.data() + offset, output.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool readCommand(int fd, char& command) noexcept {
    while (true) {
        const ssize_t count = ::read(fd, &command, 1U);
        if (count == 1) {
            return true;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
}

class ControlledProcess {
  public:
    ControlledProcess() = default;
    ControlledProcess(pid_t pid, int command_fd, int output_fd) noexcept
        : pid_(pid), command_fd_(command_fd), output_fd_(output_fd) {}
    ~ControlledProcess() { terminate(); }

    ControlledProcess(const ControlledProcess&) = delete;
    ControlledProcess& operator=(const ControlledProcess&) = delete;
    ControlledProcess(ControlledProcess&& other) noexcept
        : pid_(std::exchange(other.pid_, -1)), command_fd_(std::exchange(other.command_fd_, -1)),
          output_fd_(std::exchange(other.output_fd_, -1)), output_(std::move(other.output_)) {}
    ControlledProcess& operator=(ControlledProcess&& other) noexcept {
        if (this != &other) {
            terminate();
            pid_ = std::exchange(other.pid_, -1);
            command_fd_ = std::exchange(other.command_fd_, -1);
            output_fd_ = std::exchange(other.output_fd_, -1);
            output_ = std::move(other.output_);
        }
        return *this;
    }

    static ControlledProcess spawn(const std::function<int(int, int)>& worker) {
        int commands[2] = {-1, -1};
        int output[2] = {-1, -1};
        if (::pipe2(commands, O_CLOEXEC) != 0 || ::pipe2(output, O_CLOEXEC) != 0) {
            throw std::runtime_error("pipe2 failed");
        }
        const pid_t child = ::fork();
        if (child < 0) {
            throw std::runtime_error("fork failed");
        }
        if (child == 0) {
            (void)::close(commands[1]);
            (void)::close(output[0]);
            const int result = worker(commands[0], output[1]);
            (void)::close(commands[0]);
            (void)::close(output[1]);
            ::_exit(result);
        }
        (void)::close(commands[0]);
        (void)::close(output[1]);
        return ControlledProcess{child, commands[1], output[0]};
    }

    pid_t pid() const noexcept { return pid_; }

    bool send(char command) noexcept {
        while (true) {
            const ssize_t count = ::write(command_fd_, &command, 1U);
            if (count == 1) {
                return true;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
    }

    std::optional<std::string> readLine(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            const std::size_t newline = output_.find('\n');
            if (newline != std::string::npos) {
                std::string line = output_.substr(0U, newline);
                output_.erase(0U, newline + 1U);
                return line;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return std::nullopt;
            }
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            pollfd descriptor{output_fd_, POLLIN | POLLHUP, 0};
            int result = 0;
            do {
                result = ::poll(&descriptor, 1, static_cast<int>(remaining.count() + 1));
            } while (result < 0 && errno == EINTR);
            if (result <= 0) {
                return std::nullopt;
            }
            char buffer[256];
            const ssize_t count = ::read(output_fd_, buffer, sizeof(buffer));
            if (count > 0) {
                output_.append(buffer, static_cast<std::size_t>(count));
                continue;
            }
            return std::nullopt;
        }
    }

    int signalAndWait(int signal) noexcept {
        if (pid_ <= 0) {
            return 255;
        }
        (void)::kill(pid_, signal);
        int status = 0;
        while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
        }
        pid_ = -1;
        closePipes();
        return WIFSIGNALED(status) ? 128 + WTERMSIG(status)
                                   : (WIFEXITED(status) ? WEXITSTATUS(status) : 255);
    }

    int stopNormally() noexcept {
        if (pid_ <= 0) {
            return 0;
        }
        (void)send('Q');
        int status = 0;
        while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
        }
        pid_ = -1;
        closePipes();
        return WIFEXITED(status) ? WEXITSTATUS(status) : 255;
    }

  private:
    void closePipes() noexcept {
        if (command_fd_ >= 0) {
            (void)::close(command_fd_);
            command_fd_ = -1;
        }
        if (output_fd_ >= 0) {
            (void)::close(output_fd_);
            output_fd_ = -1;
        }
    }

    void terminate() noexcept {
        if (pid_ > 0) {
            (void)::kill(pid_, SIGKILL);
            (void)::waitpid(pid_, nullptr, 0);
            pid_ = -1;
        }
        closePipes();
    }

    pid_t pid_{-1};
    int command_fd_{-1};
    int output_fd_{-1};
    std::string output_;
};

class RegistryProcess {
  public:
    explicit RegistryProcess(std::string path) : path_(std::move(path)) {
        pid_ = ::fork();
        if (pid_ < 0) {
            throw std::runtime_error("registry fork failed");
        }
        if (pid_ == 0) {
            const int null_fd = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (null_fd >= 0) {
                (void)::dup2(null_fd, STDOUT_FILENO);
                (void)::dup2(null_fd, STDERR_FILENO);
                (void)::close(null_fd);
            }
            ::execl(MW_REGISTRYD_PATH, MW_REGISTRYD_PATH, "--socket", path_.c_str(),
                    "--heartbeat-interval-ms", "20", "--suspect-timeout-ms", "80",
                    "--dead-timeout-ms", "180", static_cast<char*>(nullptr));
            ::_exit(127);
        }
    }

    ~RegistryProcess() {
        if (pid_ > 0) {
            (void)::kill(pid_, SIGTERM);
            (void)::waitpid(pid_, nullptr, 0);
        }
    }

    bool ready() const {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline) {
            try {
                mw::detail::RegistryClient client{registryConfig(path_)};
                (void)client.listNodes();
                return true;
            } catch (...) {
                std::this_thread::sleep_for(10ms);
            }
        }
        return false;
    }

    bool alive() const noexcept { return pid_ > 0 && ::kill(pid_, 0) == 0; }

  private:
    std::string path_;
    pid_t pid_{-1};
};

ControlledProcess spawnPublisher(const std::string& registry_path, const std::string& node_name,
                                 const std::string& topic, std::uint32_t chunk_count) {
    return ControlledProcess::spawn([=](int command_fd, int output_fd) {
        try {
            mw::Context context{node_name, registryConfig(registry_path)};
            mw::PublisherConfig config;
            config.max_message_size = 256U;
            config.transport = mw::TransportType::SharedMemory;
            config.memory_pool.size_classes = {{256U, chunk_count}};
            auto publisher = context.createPublisher(topic, config);
            if (!writeLine(output_fd, "READY")) {
                return 2;
            }
            char command = 0;
            while (readCommand(command_fd, command)) {
                if (command == 'Q') {
                    return 0;
                }
                if (command == 'L') {
                    auto loan = publisher.loan(64U);
                    if (!writeLine(output_fd,
                                   "L " + std::to_string(static_cast<int>(loan.error())))) {
                        return 3;
                    }
                    continue;
                }
                if (command == 'P') {
                    auto loan = publisher.loan(64U);
                    mw::PublishResult result;
                    if (loan) {
                        std::memset(loan.data(), 0x5A, loan.size());
                        result = loan.publish();
                    } else {
                        result.error = loan.error();
                    }
                    if (!writeLine(output_fd, "P " +
                                                  std::to_string(static_cast<int>(result.error)) +
                                                  " " + std::to_string(result.sequence))) {
                        return 4;
                    }
                }
            }
        } catch (...) {
            return 5;
        }
        return 0;
    });
}

ControlledProcess spawnSubscriber(const std::string& registry_path, const std::string& node_name,
                                  const std::string& topic, const std::string& socket_path,
                                  std::uint32_t depth = 2U,
                                  mw::OverflowPolicy policy = mw::OverflowPolicy::DropOldest,
                                  std::chrono::milliseconds block_timeout = 5s) {
    return ControlledProcess::spawn([=](int command_fd, int output_fd) {
        try {
            mw::Context context{node_name, registryConfig(registry_path)};
            mw::SubscriberConfig config;
            config.socket_path = socket_path;
            config.max_message_size = 256U;
            config.transport = mw::TransportType::SharedMemory;
            config.queue_depth = depth;
            config.overflow_policy = policy;
            config.block_timeout = block_timeout;
            auto subscriber = context.createSubscriber(topic, config);
            std::optional<mw::SampleView> held;
            if (!writeLine(output_fd, "READY")) {
                return 2;
            }
            char command = 0;
            while (readCommand(command_fd, command)) {
                if (command == 'Q') {
                    return 0;
                }
                if (command == 'R') {
                    held.reset();
                    if (!writeLine(output_fd, "RELEASED")) {
                        return 3;
                    }
                    continue;
                }
                if (command == 'T') {
                    const auto deadline = std::chrono::steady_clock::now() + 5s;
                    while (std::chrono::steady_clock::now() < deadline) {
                        held = subscriber.waitAndTakeView(250ms);
                        if (held.has_value()) {
                            break;
                        }
                        const mw::ErrorCode error = subscriber.lastError();
                        if (error != mw::ErrorCode::Timeout &&
                            error != mw::ErrorCode::ConnectionLost &&
                            error != mw::ErrorCode::SharedMemoryNotFound) {
                            (void)writeLine(output_fd,
                                            "ERROR " + std::to_string(static_cast<int>(error)));
                            return 4;
                        }
                    }
                    if (!held.has_value()) {
                        (void)writeLine(output_fd, "ERROR timeout");
                        return 5;
                    }
                    if (!writeLine(output_fd, "VIEW " + std::to_string(held->sequence()) + " " +
                                                  std::to_string(held->poolId()))) {
                        return 6;
                    }
                }
            }
        } catch (const std::exception& error) {
            (void)writeLine(output_fd, "EXCEPTION " + std::string{error.what()});
            return 7;
        } catch (...) {
            (void)writeLine(output_fd, "EXCEPTION unknown");
            return 7;
        }
        return 0;
    });
}

int responseError(const std::string& response) {
    std::istringstream input{response};
    std::string kind;
    int error = -1;
    input >> kind >> error;
    return error;
}

int responseError(const std::optional<std::string>& response) {
    return response.has_value() ? responseError(*response) : -1;
}

bool isViewResponse(const std::optional<std::string>& response) {
    return response.has_value() && response->rfind("VIEW ", 0U) == 0U;
}

bool waitForNodeCount(const std::string& registry_path, std::size_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            mw::detail::RegistryClient client{registryConfig(registry_path)};
            if (client.listNodes().size() == expected) {
                return true;
            }
        } catch (...) {
        }
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

bool sharedMemoryExists(const std::string& name) {
    return !name.empty() && std::filesystem::exists("/dev/shm/" + name.substr(1U));
}

bool waitForSharedMemoryGone(const std::string& name) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!sharedMemoryExists(name)) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

bool waitForPathGone(const std::string& path) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!std::filesystem::exists(path)) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

std::optional<std::string> publisherPool(const std::string& registry_path,
                                         const std::string& topic) {
    try {
        mw::detail::RegistryClient client{registryConfig(registry_path)};
        const auto info = client.queryTopic(topic);
        if (info.publisher_count == 1U && !info.pool.shm_name.empty()) {
            return info.pool.shm_name;
        }
    } catch (...) {
    }
    return std::nullopt;
}

TEST(LivenessIntegrationTest, OpenSocketTimesOutAndSuspectedHeartbeatRecovers) {
    const std::string suffix = std::to_string(::getpid());
    const std::string registry_path = "/tmp/mw_test_liveness_registry_" + suffix + ".sock";
    RegistryProcess registry{registry_path};
    ASSERT_TRUE(registry.ready());

    mw::detail::RegistryClient primary{registryConfig(registry_path)};
    const auto registration = primary.registerNode("stalled_node");
    mw::detail::RegistryClient heartbeat{registryConfig(registry_path)};
    heartbeat.attachHeartbeat(registration.node_id, registration.session_id);
    EXPECT_EQ(heartbeat.heartbeat(registration.node_id, registration.session_id).state,
              mw::LivenessState::Alive);

    mw::detail::RegistryClient wrong_connection{registryConfig(registry_path)};
    EXPECT_THROW((void)wrong_connection.heartbeat(registration.node_id, registration.session_id),
                 mw::MiddlewareError);

    const auto suspect_deadline = std::chrono::steady_clock::now() + 500ms;
    bool suspected = false;
    while (std::chrono::steady_clock::now() < suspect_deadline) {
        const auto nodes = wrong_connection.listNodes();
        if (!nodes.empty() && nodes.front().liveness == mw::LivenessState::Suspected) {
            suspected = true;
            break;
        }
        std::this_thread::sleep_for(10ms);
    }
    ASSERT_TRUE(suspected);
    EXPECT_EQ(heartbeat.heartbeat(registration.node_id, registration.session_id).state,
              mw::LivenessState::Alive);

    ASSERT_TRUE(waitForNodeCount(registry_path, 0U));
    EXPECT_TRUE(registry.alive());
    EXPECT_THROW((void)heartbeat.heartbeat(registration.node_id, registration.session_id),
                 mw::MiddlewareError);
    mw::detail::RegistryClient replacement_client{registryConfig(registry_path)};
    const auto replacement = replacement_client.registerNode("stalled_node");
    EXPECT_NE(replacement.node_id, registration.node_id);
    replacement_client.unregisterNode(replacement.node_id);
}

TEST(CrashRecoveryIntegrationTest, SubscriberSigkillRecoversLiveViewAndRemainingPeersContinue) {
    const std::string suffix = std::to_string(::getpid());
    const std::string registry_path = "/tmp/mw_test_subscriber_crash_registry_" + suffix + ".sock";
    const std::string topic = "/test/subscriber_crash";
    RegistryProcess registry{registry_path};
    ASSERT_TRUE(registry.ready());

    std::vector<ControlledProcess> subscribers;
    for (int index = 0; index < 4; ++index) {
        subscribers.push_back(
            spawnSubscriber(registry_path, "crash_test_subscriber_" + std::to_string(index), topic,
                            "/tmp/mw_test_subscriber_crash_data_" + suffix + "_" +
                                std::to_string(index) + ".sock"));
        ASSERT_EQ(subscribers.back().readLine(2s), std::optional<std::string>{"READY"});
    }
    ControlledProcess publisher = spawnPublisher(registry_path, "crash_test_publisher", topic, 1U);
    ASSERT_EQ(publisher.readLine(2s), std::optional<std::string>{"READY"});
    ASSERT_TRUE(publisher.send('P'));
    ASSERT_EQ(responseError(publisher.readLine(3s)), static_cast<int>(mw::ErrorCode::Ok));
    for (auto& subscriber : subscribers) {
        ASSERT_TRUE(subscriber.send('T'));
        ASSERT_TRUE(isViewResponse(subscriber.readLine(3s)));
    }
    ASSERT_TRUE(publisher.send('L'));
    ASSERT_EQ(responseError(publisher.readLine(2s)),
              static_cast<int>(mw::ErrorCode::PoolExhausted));

    const pid_t dead_pid = subscribers[1].pid();
    const std::string dead_socket_path = "/tmp/mw_test_subscriber_crash_data_" + suffix + "_1.sock";
    EXPECT_EQ(subscribers[1].signalAndWait(SIGKILL), 128 + SIGKILL);
    for (std::size_t index : {0U, 2U, 3U}) {
        ASSERT_TRUE(subscribers[index].send('R'));
        ASSERT_EQ(subscribers[index].readLine(2s), std::optional<std::string>{"RELEASED"});
    }
    ASSERT_TRUE(waitForNodeCount(registry_path, 4U));
    const std::string dead_queue_prefix = "mw_queue_" + std::to_string(dead_pid) + "_";
    const auto queue_deadline = std::chrono::steady_clock::now() + 3s;
    bool dead_queue_gone = false;
    while (std::chrono::steady_clock::now() < queue_deadline) {
        dead_queue_gone = true;
        for (const auto& entry : std::filesystem::directory_iterator{"/dev/shm"}) {
            if (entry.path().filename().string().rfind(dead_queue_prefix, 0U) == 0U) {
                dead_queue_gone = false;
                break;
            }
        }
        if (dead_queue_gone) {
            break;
        }
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_TRUE(dead_queue_gone);
    EXPECT_TRUE(waitForPathGone(dead_socket_path));

    ASSERT_TRUE(publisher.send('L'));
    ASSERT_EQ(responseError(publisher.readLine(2s)), static_cast<int>(mw::ErrorCode::Ok));
    ASSERT_TRUE(publisher.send('P'));
    ASSERT_EQ(responseError(publisher.readLine(3s)), static_cast<int>(mw::ErrorCode::Ok));
    for (std::size_t index : {0U, 2U, 3U}) {
        ASSERT_TRUE(subscribers[index].send('T'));
        ASSERT_TRUE(isViewResponse(subscribers[index].readLine(3s)));
        ASSERT_TRUE(subscribers[index].send('R'));
        ASSERT_EQ(subscribers[index].readLine(2s), std::optional<std::string>{"RELEASED"});
    }

    const std::string replacement_socket_path =
        "/tmp/mw_test_subscriber_replacement_" + suffix + ".sock";
    subscribers[1] = spawnSubscriber(registry_path, "crash_test_subscriber_replacement", topic,
                                     replacement_socket_path);
    ASSERT_EQ(subscribers[1].readLine(2s), std::optional<std::string>{"READY"});
    ASSERT_TRUE(publisher.send('P'));
    ASSERT_EQ(responseError(publisher.readLine(3s)), static_cast<int>(mw::ErrorCode::Ok));
    for (auto& subscriber : subscribers) {
        ASSERT_TRUE(subscriber.send('T'));
        ASSERT_TRUE(isViewResponse(subscriber.readLine(3s)));
        ASSERT_TRUE(subscriber.send('R'));
        ASSERT_EQ(subscriber.readLine(2s), std::optional<std::string>{"RELEASED"});
    }
    EXPECT_EQ(subscribers[1].signalAndWait(SIGKILL), 128 + SIGKILL);
    ASSERT_TRUE(waitForNodeCount(registry_path, 4U));
    EXPECT_TRUE(waitForPathGone(replacement_socket_path));
    ASSERT_TRUE(publisher.send('L'));
    ASSERT_EQ(responseError(publisher.readLine(2s)), static_cast<int>(mw::ErrorCode::Ok));
    EXPECT_TRUE(registry.alive());
    EXPECT_EQ(publisher.stopNormally(), 0);
    for (auto& subscriber : subscribers) {
        EXPECT_EQ(subscriber.stopNormally(), 0);
    }
}

TEST(CrashRecoveryIntegrationTest, PublisherSigkillCleansPoolAndReconnectsRepeatedly) {
    const std::string suffix = std::to_string(::getpid());
    const std::string registry_path = "/tmp/mw_test_publisher_crash_registry_" + suffix + ".sock";
    const std::string topic = "/test/publisher_crash";
    RegistryProcess registry{registry_path};
    ASSERT_TRUE(registry.ready());
    ControlledProcess subscriber =
        spawnSubscriber(registry_path, "publisher_crash_survivor", topic,
                        "/tmp/mw_test_publisher_crash_data_" + suffix + ".sock");
    ASSERT_EQ(subscriber.readLine(2s), std::optional<std::string>{"READY"});

    for (int cycle = 0; cycle < 2; ++cycle) {
        SCOPED_TRACE("publisher crash cycle " + std::to_string(cycle));
        ControlledProcess publisher =
            spawnPublisher(registry_path, "crashing_publisher_" + std::to_string(cycle), topic, 1U);
        ASSERT_EQ(publisher.readLine(2s), std::optional<std::string>{"READY"});
        const auto pool = publisherPool(registry_path, topic);
        ASSERT_TRUE(pool.has_value());
        ASSERT_TRUE(sharedMemoryExists(*pool));
        ASSERT_TRUE(publisher.send('P'));
        ASSERT_EQ(responseError(publisher.readLine(3s)), static_cast<int>(mw::ErrorCode::Ok));
        ASSERT_TRUE(subscriber.send('T'));
        const auto view_response = subscriber.readLine(3s);
        ASSERT_TRUE(isViewResponse(view_response))
            << (view_response.has_value() ? *view_response : "no child response");
        ASSERT_TRUE(subscriber.send('R'));
        ASSERT_EQ(subscriber.readLine(2s), std::optional<std::string>{"RELEASED"});
        EXPECT_EQ(publisher.signalAndWait(SIGKILL), 128 + SIGKILL);
        EXPECT_TRUE(waitForSharedMemoryGone(*pool));
        ASSERT_TRUE(waitForNodeCount(registry_path, 1U));
        EXPECT_TRUE(registry.alive());
    }

    ControlledProcess replacement =
        spawnPublisher(registry_path, "replacement_publisher", topic, 1U);
    ASSERT_EQ(replacement.readLine(2s), std::optional<std::string>{"READY"});
    ASSERT_TRUE(replacement.send('P'));
    ASSERT_EQ(responseError(replacement.readLine(3s)), static_cast<int>(mw::ErrorCode::Ok));
    ASSERT_TRUE(subscriber.send('T'));
    ASSERT_TRUE(isViewResponse(subscriber.readLine(3s)));
    ASSERT_TRUE(subscriber.send('R'));
    ASSERT_EQ(subscriber.readLine(2s), std::optional<std::string>{"RELEASED"});
    EXPECT_EQ(replacement.stopNormally(), 0);
    EXPECT_EQ(subscriber.stopNormally(), 0);
}

TEST(CrashRecoveryIntegrationTest, BlockedPublisherWakesWhenSubscriberIsKilled) {
    const std::string suffix = std::to_string(::getpid());
    const std::string registry_path = "/tmp/mw_test_block_crash_registry_" + suffix + ".sock";
    const std::string topic = "/test/block_crash";
    RegistryProcess registry{registry_path};
    ASSERT_TRUE(registry.ready());
    ControlledProcess subscriber =
        spawnSubscriber(registry_path, "blocked_subscriber", topic,
                        "/tmp/mw_test_block_crash_data_" + suffix + ".sock", 1U,
                        mw::OverflowPolicy::BlockWithTimeout, 5s);
    ASSERT_EQ(subscriber.readLine(2s), std::optional<std::string>{"READY"});
    ControlledProcess publisher = spawnPublisher(registry_path, "blocked_publisher", topic, 2U);
    ASSERT_EQ(publisher.readLine(2s), std::optional<std::string>{"READY"});
    ASSERT_TRUE(publisher.send('P'));
    ASSERT_EQ(responseError(publisher.readLine(3s)), static_cast<int>(mw::ErrorCode::Ok));
    ASSERT_TRUE(publisher.send('P'));
    EXPECT_FALSE(publisher.readLine(100ms).has_value());
    EXPECT_EQ(subscriber.signalAndWait(SIGKILL), 128 + SIGKILL);
    const auto unblocked = publisher.readLine(2s);
    ASSERT_TRUE(unblocked.has_value());
    EXPECT_NE(responseError(*unblocked), static_cast<int>(mw::ErrorCode::Ok));

    subscriber = spawnSubscriber(registry_path, "blocked_subscriber_replacement", topic,
                                 "/tmp/mw_test_block_replacement_" + suffix + ".sock", 1U,
                                 mw::OverflowPolicy::BlockWithTimeout, 5s);
    ASSERT_EQ(subscriber.readLine(2s), std::optional<std::string>{"READY"});
    ASSERT_TRUE(publisher.send('P'));
    ASSERT_EQ(responseError(publisher.readLine(3s)), static_cast<int>(mw::ErrorCode::Ok));
    ASSERT_TRUE(subscriber.send('T'));
    ASSERT_TRUE(isViewResponse(subscriber.readLine(3s)));
    ASSERT_TRUE(subscriber.send('R'));
    ASSERT_EQ(subscriber.readLine(2s), std::optional<std::string>{"RELEASED"});
    EXPECT_TRUE(registry.alive());
    EXPECT_EQ(publisher.stopNormally(), 0);
    EXPECT_EQ(subscriber.stopNormally(), 0);
}

} // namespace
