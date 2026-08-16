#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <poll.h>
#include <set>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#ifndef MW_REGISTRYD_PATH
#error MW_REGISTRYD_PATH must be defined
#endif
#ifndef MW_MWCTL_PATH
#error MW_MWCTL_PATH must be defined
#endif
#ifndef MW_ROS2_TO_MW_PATH
#error MW_ROS2_TO_MW_PATH must be defined
#endif
#ifndef MW_MW_TO_ROS2_PATH
#error MW_MW_TO_ROS2_PATH must be defined
#endif
#ifndef MW_TEST_PEER_PATH
#error MW_TEST_PEER_PATH must be defined
#endif
#ifndef MW_ROS2_CLI_PATH
#error MW_ROS2_CLI_PATH must be defined
#endif

namespace {

using namespace std::chrono_literals;

class ChildProcess {
  public:
    ChildProcess() = default;
    ~ChildProcess() { stop(SIGKILL); }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    ChildProcess(ChildProcess&& other) noexcept
        : pid_(std::exchange(other.pid_, -1)), status_(other.status_) {}

    ChildProcess& operator=(ChildProcess&& other) noexcept {
        if (this != &other) {
            stop(SIGKILL);
            pid_ = std::exchange(other.pid_, -1);
            status_ = other.status_;
        }
        return *this;
    }

    static ChildProcess start(const std::string& executable,
                              const std::vector<std::string>& arguments) {
        const pid_t child = ::fork();
        if (child < 0) {
            throw std::runtime_error("fork failed");
        }
        if (child == 0) {
            std::vector<std::string> storage;
            storage.reserve(arguments.size() + 1U);
            storage.push_back(executable);
            storage.insert(storage.end(), arguments.begin(), arguments.end());
            std::vector<char*> argv;
            argv.reserve(storage.size() + 1U);
            for (std::string& item : storage) {
                argv.push_back(item.data());
            }
            argv.push_back(nullptr);
            ::execv(executable.c_str(), argv.data());
            _exit(127);
        }
        ChildProcess process;
        process.pid_ = child;
        return process;
    }

    bool running() {
        if (pid_ <= 0) {
            return false;
        }
        int status = 0;
        const pid_t result = ::waitpid(pid_, &status, WNOHANG);
        if (result == pid_) {
            status_ = status;
            pid_ = -1;
            return false;
        }
        return result == 0;
    }

    std::optional<int> wait(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (running()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return std::nullopt;
            }
            std::this_thread::sleep_for(5ms);
        }
        return exitCode();
    }

    std::optional<int> stop(int signal, std::chrono::milliseconds timeout = 5s) {
        if (running()) {
            (void)::kill(pid_, signal);
        }
        auto result = wait(timeout);
        if (!result.has_value() && pid_ > 0) {
            (void)::kill(pid_, SIGKILL);
            result = wait(2s);
        }
        return result;
    }

    pid_t pid() const noexcept { return pid_; }

  private:
    std::optional<int> exitCode() const noexcept {
        if (!status_.has_value()) {
            return std::nullopt;
        }
        if (WIFEXITED(*status_)) {
            return WEXITSTATUS(*status_);
        }
        if (WIFSIGNALED(*status_)) {
            return 128 + WTERMSIG(*status_);
        }
        return std::nullopt;
    }

    pid_t pid_{-1};
    std::optional<int> status_;
};

struct CommandResult {
    int exit_code{-1};
    std::string output;
};

CommandResult runCapture(const std::string& executable, const std::vector<std::string>& arguments,
                         std::chrono::milliseconds timeout = 5s) {
    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) {
        throw std::runtime_error("pipe failed");
    }
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        throw std::runtime_error("fork failed");
    }
    if (child == 0) {
        ::close(pipe_fds[0]);
        (void)::dup2(pipe_fds[1], STDOUT_FILENO);
        (void)::dup2(pipe_fds[1], STDERR_FILENO);
        ::close(pipe_fds[1]);
        std::vector<std::string> storage;
        storage.reserve(arguments.size() + 1U);
        storage.push_back(executable);
        storage.insert(storage.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        for (std::string& item : storage) {
            argv.push_back(item.data());
        }
        argv.push_back(nullptr);
        ::execv(executable.c_str(), argv.data());
        _exit(127);
    }

    ::close(pipe_fds[1]);
    const int current_flags = ::fcntl(pipe_fds[0], F_GETFL, 0);
    (void)::fcntl(pipe_fds[0], F_SETFL, current_flags | O_NONBLOCK);
    CommandResult result;
    bool exited = false;
    bool eof = false;
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while ((!exited || !eof) && std::chrono::steady_clock::now() < deadline) {
        std::array<char, 1024> buffer{};
        for (;;) {
            const ssize_t count = ::read(pipe_fds[0], buffer.data(), buffer.size());
            if (count > 0) {
                result.output.append(buffer.data(), static_cast<std::size_t>(count));
            } else {
                if (count == 0) {
                    eof = true;
                }
                break;
            }
        }
        if (!exited) {
            const pid_t wait_result = ::waitpid(child, &status, WNOHANG);
            exited = wait_result == child;
        }
        if (!exited || !eof) {
            pollfd descriptor{pipe_fds[0], POLLIN | POLLHUP, 0};
            (void)::poll(&descriptor, 1U, 10);
        }
    }
    ::close(pipe_fds[0]);
    if (!exited) {
        (void)::kill(child, SIGKILL);
        (void)::waitpid(child, &status, 0);
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

std::set<std::string> projectSharedMemoryObjects() {
    std::set<std::string> names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/shm", error)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("mw_pool_", 0U) == 0U || name.rfind("mw_queue_", 0U) == 0U) {
            names.insert(name);
        }
    }
    return names;
}

struct ScenarioPaths {
    std::string id;
    std::string registry;
    std::string data_socket;
    std::string ready_file;
    std::string ros_topic;
    std::string mw_topic;
    std::string bridge_node;
    std::string bridge_mw_node;
};

ScenarioPaths makePaths(const char* label) {
    static std::atomic<unsigned int> counter{1U};
    const std::string id = "case_" + std::to_string(::getpid()) + "_" +
                           std::to_string(counter.fetch_add(1U)) + "_" + label;
    return ScenarioPaths{
        id,
        "/tmp/mw_adapter_" + id + ".reg",
        "/tmp/mw_adapter_" + id + ".data",
        "/tmp/mw_adapter_" + id + ".ready",
        "/adapter/ros/" + id,
        "/adapter/data/" + id,
        "adapter_bridge_" + id,
        "adapter_bridge_mw_" + id,
    };
}

std::vector<std::string> peerArguments(const ScenarioPaths& paths, const std::string& role,
                                       const std::string& message_type,
                                       const std::string& text = "adapter-string-payload",
                                       const std::string& transport = "shm") {
    return {
        "--role",         role,
        "--registry",     paths.registry,
        "--ros-topic",    paths.ros_topic,
        "--mw-topic",     paths.mw_topic,
        "--message-type", message_type,
        "--socket",       paths.data_socket,
        "--ready-file",   paths.ready_file,
        "--text",         text,
        "--transport",    transport,
        "--timeout-ms",   "30000",
    };
}

std::vector<std::string> bridgeArguments(const ScenarioPaths& paths,
                                         const std::string& message_type,
                                         const std::string& transport = "shm") {
    return {
        "--ros-args",
        "-r",
        "__node:=" + paths.bridge_node,
        "-p",
        "registry_socket:=" + paths.registry,
        "-p",
        "ros_topic:=" + paths.ros_topic,
        "-p",
        "mw_topic:=" + paths.mw_topic,
        "-p",
        "message_type:=" + message_type,
        "-p",
        "transport:=" + transport,
        "-p",
        "max_message_size:=4194304",
        "-p",
        "mw_node_name:=" + paths.bridge_mw_node,
        "-p",
        "mw_socket_path:=" + paths.data_socket,
    };
}

CommandResult mwctl(const ScenarioPaths& paths, const std::vector<std::string>& command) {
    std::vector<std::string> arguments{"--registry", paths.registry};
    arguments.insert(arguments.end(), command.begin(), command.end());
    return runCapture(MW_MWCTL_PATH, arguments);
}

bool waitForTopic(const ScenarioPaths& paths, const std::string& expected_type) {
    return waitUntil(
        [&]() {
            const CommandResult result = mwctl(paths, {"topic", "info", paths.mw_topic});
            return result.exit_code == 0 && result.output.find(expected_type) != std::string::npos;
        },
        10s);
}

bool waitForNodeAbsent(const ScenarioPaths& paths) {
    return waitUntil(
        [&]() {
            const CommandResult result = mwctl(paths, {"node", "list"});
            return result.exit_code == 0 &&
                   result.output.find(paths.bridge_mw_node) == std::string::npos;
        },
        5s);
}

class Scenario {
  public:
    explicit Scenario(const char* label) : paths(makePaths(label)) {
        (void)::unlink(paths.registry.c_str());
        (void)::unlink(paths.data_socket.c_str());
        (void)::unlink(paths.ready_file.c_str());
    }

    ~Scenario() {
        bridge.stop(SIGKILL);
        first_peer.stop(SIGKILL);
        second_peer.stop(SIGKILL);
        registry.stop(SIGTERM);
        (void)::unlink(paths.registry.c_str());
        (void)::unlink(paths.data_socket.c_str());
        (void)::unlink(paths.ready_file.c_str());
    }

    void startRegistry() {
        registry = ChildProcess::start(MW_REGISTRYD_PATH, {"--socket", paths.registry});
    }

    ScenarioPaths paths;
    ChildProcess registry;
    ChildProcess bridge;
    ChildProcess first_peer;
    ChildProcess second_peer;
};

class BridgeIntegrationTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        const std::string domain = std::to_string(120 + (::getpid() % 80));
        ASSERT_EQ(::setenv("ROS_DOMAIN_ID", domain.c_str(), 1), 0);
        ASSERT_EQ(::unsetenv("ROS_LOCALHOST_ONLY"), 0);
        ASSERT_EQ(::setenv("ROS_AUTOMATIC_DISCOVERY_RANGE", "LOCALHOST", 1), 0);
    }

    void runRosToMiddleware(const char* label, const std::string& message_type,
                            const std::string& transport = "shm") {
        Scenario scenario{label};
        scenario.startRegistry();
        ASSERT_TRUE(
            waitUntil([&]() { return ::access(scenario.paths.registry.c_str(), F_OK) == 0; }, 5s));

        scenario.first_peer = ChildProcess::start(
            MW_TEST_PEER_PATH, peerArguments(scenario.paths, "mw-subscriber", message_type,
                                             "adapter-string-payload", transport));
        ASSERT_TRUE(waitUntil(
            [&]() { return ::access(scenario.paths.data_socket.c_str(), F_OK) == 0; }, 5s));

        scenario.bridge = ChildProcess::start(
            MW_ROS2_TO_MW_PATH, bridgeArguments(scenario.paths, message_type, transport));
        ASSERT_TRUE(waitForTopic(scenario.paths, message_type));

        scenario.second_peer = ChildProcess::start(
            MW_TEST_PEER_PATH, peerArguments(scenario.paths, "ros-publisher", message_type));
        ASSERT_EQ(scenario.second_peer.wait(30s), std::optional<int>{0});
        ASSERT_EQ(scenario.first_peer.wait(30s), std::optional<int>{0});
        ASSERT_TRUE(scenario.bridge.running());

        const CommandResult nodes = mwctl(scenario.paths, {"node", "list"});
        ASSERT_EQ(nodes.exit_code, 0) << nodes.output;
        EXPECT_NE(nodes.output.find(scenario.paths.bridge_mw_node), std::string::npos);

        EXPECT_EQ(scenario.bridge.stop(SIGINT), std::optional<int>{0});
        EXPECT_TRUE(waitForNodeAbsent(scenario.paths));
        EXPECT_TRUE(waitUntil(
            [&]() { return ::access(scenario.paths.data_socket.c_str(), F_OK) != 0; }, 5s));
    }

    void runMiddlewareToRos(const char* label, const std::string& message_type,
                            const std::string& transport = "shm") {
        Scenario scenario{label};
        scenario.startRegistry();
        ASSERT_TRUE(
            waitUntil([&]() { return ::access(scenario.paths.registry.c_str(), F_OK) == 0; }, 5s));

        scenario.first_peer = ChildProcess::start(
            MW_TEST_PEER_PATH, peerArguments(scenario.paths, "ros-subscriber", message_type,
                                             "adapter-string-payload", transport));
        scenario.bridge = ChildProcess::start(
            MW_MW_TO_ROS2_PATH, bridgeArguments(scenario.paths, message_type, transport));
        ASSERT_TRUE(waitForTopic(scenario.paths, message_type));
        ASSERT_TRUE(waitUntil(
            [&]() { return ::access(scenario.paths.ready_file.c_str(), F_OK) == 0; }, 10s));

        scenario.second_peer = ChildProcess::start(
            MW_TEST_PEER_PATH, peerArguments(scenario.paths, "mw-publisher", message_type,
                                             "adapter-string-payload", transport));
        ASSERT_EQ(scenario.second_peer.wait(30s), std::optional<int>{0});
        ASSERT_EQ(scenario.first_peer.wait(30s), std::optional<int>{0});
        ASSERT_TRUE(scenario.bridge.running());

        EXPECT_EQ(scenario.bridge.stop(SIGINT), std::optional<int>{0});
        EXPECT_TRUE(waitForNodeAbsent(scenario.paths));
        EXPECT_TRUE(waitUntil(
            [&]() { return ::access(scenario.paths.data_socket.c_str(), F_OK) != 0; }, 5s));
    }
};

TEST_F(BridgeIntegrationTest, StringRos2ToMiddleware) {
    runRosToMiddleware("str_r2m", "std_msgs/msg/String");
}

TEST_F(BridgeIntegrationTest, StringMiddlewareToRos2) {
    runMiddlewareToRos("str_m2r", "std_msgs/msg/String");
}

TEST_F(BridgeIntegrationTest, StringUdsRos2ToMiddleware) {
    runRosToMiddleware("str_uds_r2m", "std_msgs/msg/String", "uds");
}

TEST_F(BridgeIntegrationTest, StringUdsMiddlewareToRos2) {
    runMiddlewareToRos("str_uds_m2r", "std_msgs/msg/String", "uds");
}

TEST_F(BridgeIntegrationTest, TwistRos2ToMiddleware) {
    runRosToMiddleware("twist_r2m", "geometry_msgs/msg/Twist");
}

TEST_F(BridgeIntegrationTest, TwistMiddlewareToRos2) {
    runMiddlewareToRos("twist_m2r", "geometry_msgs/msg/Twist");
}

TEST_F(BridgeIntegrationTest, LargeImageRos2ToMiddleware) {
    runRosToMiddleware("image_r2m", "sensor_msgs/msg/Image");
}

TEST_F(BridgeIntegrationTest, LargeImageMiddlewareToRos2) {
    runMiddlewareToRos("image_m2r", "sensor_msgs/msg/Image");
}

TEST_F(BridgeIntegrationTest, Ros2TopicPubCliReachesMiddleware) {
    Scenario scenario{"cli_r2m"};
    scenario.startRegistry();
    ASSERT_TRUE(
        waitUntil([&]() { return ::access(scenario.paths.registry.c_str(), F_OK) == 0; }, 5s));
    scenario.first_peer = ChildProcess::start(
        MW_TEST_PEER_PATH, peerArguments(scenario.paths, "mw-subscriber", "std_msgs/msg/String",
                                         "adapter-cli-payload"));
    ASSERT_TRUE(
        waitUntil([&]() { return ::access(scenario.paths.data_socket.c_str(), F_OK) == 0; }, 5s));
    scenario.bridge = ChildProcess::start(MW_ROS2_TO_MW_PATH,
                                          bridgeArguments(scenario.paths, "std_msgs/msg/String"));
    ASSERT_TRUE(waitForTopic(scenario.paths, "std_msgs/msg/String"));

    scenario.second_peer = ChildProcess::start(
        MW_ROS2_CLI_PATH, {"topic", "pub", "--once", scenario.paths.ros_topic,
                           "std_msgs/msg/String", "{data: adapter-cli-payload}"});
    ASSERT_EQ(scenario.second_peer.wait(30s), std::optional<int>{0});
    ASSERT_EQ(scenario.first_peer.wait(30s), std::optional<int>{0});
    EXPECT_TRUE(scenario.bridge.running());
}

TEST_F(BridgeIntegrationTest, RejectsUnsupportedTypeAtStartup) {
    Scenario scenario{"unsupported"};
    auto arguments = bridgeArguments(scenario.paths, "sensor_msgs/msg/PointCloud2");
    scenario.bridge = ChildProcess::start(MW_ROS2_TO_MW_PATH, arguments);
    EXPECT_EQ(scenario.bridge.wait(5s), std::optional<int>{2});
}

TEST_F(BridgeIntegrationTest, ReportsUnavailableRegistryAtStartup) {
    Scenario scenario{"no_registry"};
    scenario.bridge = ChildProcess::start(MW_ROS2_TO_MW_PATH,
                                          bridgeArguments(scenario.paths, "std_msgs/msg/String"));
    EXPECT_EQ(scenario.bridge.wait(5s), std::optional<int>{2});
}

TEST_F(BridgeIntegrationTest, RegistryRejectsMiddlewareTypeMismatch) {
    Scenario scenario{"type_mismatch"};
    scenario.startRegistry();
    ASSERT_TRUE(
        waitUntil([&]() { return ::access(scenario.paths.registry.c_str(), F_OK) == 0; }, 5s));
    scenario.bridge = ChildProcess::start(MW_MW_TO_ROS2_PATH,
                                          bridgeArguments(scenario.paths, "sensor_msgs/msg/Image"));
    ASSERT_TRUE(waitForTopic(scenario.paths, "sensor_msgs/msg/Image"));

    auto arguments = peerArguments(scenario.paths, "mw-publisher", "geometry_msgs/msg/Twist");
    arguments.push_back("--expect-type-mismatch");
    scenario.first_peer = ChildProcess::start(MW_TEST_PEER_PATH, arguments);
    EXPECT_EQ(scenario.first_peer.wait(10s), std::optional<int>{0});
    EXPECT_TRUE(scenario.bridge.running());
}

TEST_F(BridgeIntegrationTest, InvalidSerializedPayloadIsNotPublishedToRos) {
    Scenario scenario{"corrupt"};
    scenario.startRegistry();
    ASSERT_TRUE(
        waitUntil([&]() { return ::access(scenario.paths.registry.c_str(), F_OK) == 0; }, 5s));
    auto subscriber_arguments =
        peerArguments(scenario.paths, "ros-subscriber", "std_msgs/msg/String");
    subscriber_arguments[subscriber_arguments.size() - 1U] = "1500";
    scenario.first_peer = ChildProcess::start(MW_TEST_PEER_PATH, subscriber_arguments);
    scenario.bridge = ChildProcess::start(MW_MW_TO_ROS2_PATH,
                                          bridgeArguments(scenario.paths, "std_msgs/msg/String"));
    ASSERT_TRUE(waitForTopic(scenario.paths, "std_msgs/msg/String"));
    ASSERT_TRUE(
        waitUntil([&]() { return ::access(scenario.paths.ready_file.c_str(), F_OK) == 0; }, 10s));

    auto publisher_arguments = peerArguments(scenario.paths, "mw-publisher", "std_msgs/msg/String");
    publisher_arguments.push_back("--corrupt");
    scenario.second_peer = ChildProcess::start(MW_TEST_PEER_PATH, publisher_arguments);
    EXPECT_EQ(scenario.second_peer.wait(10s), std::optional<int>{0});
    EXPECT_EQ(scenario.first_peer.wait(5s), std::optional<int>{3});
    EXPECT_TRUE(scenario.bridge.running());
}

TEST_F(BridgeIntegrationTest, SigkillBridgeResourcesAreRecovered) {
    const std::set<std::string> before = projectSharedMemoryObjects();
    Scenario scenario{"crash"};
    scenario.startRegistry();
    ASSERT_TRUE(
        waitUntil([&]() { return ::access(scenario.paths.registry.c_str(), F_OK) == 0; }, 5s));
    scenario.first_peer = ChildProcess::start(
        MW_TEST_PEER_PATH, peerArguments(scenario.paths, "mw-subscriber", "std_msgs/msg/String"));
    ASSERT_TRUE(
        waitUntil([&]() { return ::access(scenario.paths.data_socket.c_str(), F_OK) == 0; }, 5s));
    scenario.bridge = ChildProcess::start(MW_ROS2_TO_MW_PATH,
                                          bridgeArguments(scenario.paths, "std_msgs/msg/String"));
    ASSERT_TRUE(waitForTopic(scenario.paths, "std_msgs/msg/String"));
    ASSERT_NE(projectSharedMemoryObjects(), before);

    EXPECT_EQ(scenario.bridge.stop(SIGKILL), std::optional<int>{128 + SIGKILL});
    EXPECT_TRUE(waitForNodeAbsent(scenario.paths));
    EXPECT_TRUE(scenario.registry.running());
    (void)scenario.first_peer.stop(SIGKILL);
    EXPECT_TRUE(waitUntil([&]() { return projectSharedMemoryObjects() == before; }, 5s));
    EXPECT_TRUE(
        waitUntil([&]() { return ::access(scenario.paths.data_socket.c_str(), F_OK) != 0; }, 5s));
}

} // namespace
