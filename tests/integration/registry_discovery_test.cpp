#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/result.hpp>

#include "detail/control_protocol.hpp"
#include "detail/registry_client.hpp"
#include "detail/socket_io.hpp"
#include "detail/unix_socket.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#ifndef MW_REGISTRYD_PATH
#error "MW_REGISTRYD_PATH is required"
#endif
#ifndef MW_MWCTL_PATH
#error "MW_MWCTL_PATH is required"
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
    ChildProcess() = default;
    explicit ChildProcess(pid_t process) : process_(process) {}
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
                if (WIFEXITED(status)) {
                    return WEXITSTATUS(status);
                }
                return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 255;
            }
            if (result < 0) {
                process_ = -1;
                return 255;
            }
            std::this_thread::sleep_for(10ms);
        }
        return 254;
    }

    void terminate() noexcept {
        if (process_ <= 0) {
            return;
        }
        (void)::kill(process_, SIGTERM);
        for (int attempt = 0; attempt < 100; ++attempt) {
            int status = 0;
            const pid_t result = ::waitpid(process_, &status, WNOHANG);
            if (result == process_ || result < 0) {
                process_ = -1;
                return;
            }
            std::this_thread::sleep_for(10ms);
        }
        (void)::kill(process_, SIGKILL);
        (void)::waitpid(process_, nullptr, 0);
        process_ = -1;
    }

  private:
    pid_t process_{-1};
};

ChildProcess spawn(const std::vector<std::string>& arguments, bool discard_output = false) {
    if (arguments.empty()) {
        throw std::invalid_argument("child process argument list must not be empty");
    }
    const pid_t child = ::fork();
    if (child < 0) {
        throw std::system_error(errno, std::generic_category(), "fork");
    }
    if (child == 0) {
        if (discard_output) {
            const int null_fd = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (null_fd >= 0) {
                (void)::dup2(null_fd, STDOUT_FILENO);
                (void)::dup2(null_fd, STDERR_FILENO);
                (void)::close(null_fd);
            }
        }
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1U);
        for (const auto& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        ::execv(argv.front(), argv.data());
        ::_exit(127);
    }
    return ChildProcess{child};
}

struct Paths {
    explicit Paths(std::string label)
        : registry("/tmp/mw_phase2_registry_" + std::to_string(::getpid()) + "_" + label + ".sock"),
          data("/tmp/mw_phase2_data_" + std::to_string(::getpid()) + "_" + label + ".sock") {}

    std::string registry;
    std::string data;
};

bool waitForRegistry(const std::string& path) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            mw::detail::RegistryClient client{mw::RegistryConfig{path}};
            (void)client.listNodes();
            return true;
        } catch (...) {
            std::this_thread::sleep_for(20ms);
        }
    }
    return false;
}

bool waitForNode(const std::string& path, const std::string& name) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            mw::detail::RegistryClient client{mw::RegistryConfig{path}};
            for (const auto& node : client.listNodes()) {
                if (node.node_name == name) {
                    return true;
                }
            }
        } catch (...) {
        }
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

ChildProcess startRegistry(const std::string& path) {
    return spawn({MW_REGISTRYD_PATH, "--socket", path}, true);
}

ChildProcess startPublisher(const Paths& paths) {
    return spawn({MW_PING_PUBLISHER_PATH, "--registry", paths.registry, "--socket", paths.data,
                  "--count", "32", "--size", "128"},
                 true);
}

ChildProcess startSubscriber(const Paths& paths) {
    return spawn({MW_PING_SUBSCRIBER_PATH, "--registry", paths.registry, "--socket", paths.data,
                  "--count", "32", "--size", "128", "--timeout-ms", "5000"},
                 true);
}

struct CommandResult {
    int exit_code{255};
    std::string output;
};

CommandResult runAndCapture(const std::vector<std::string>& arguments) {
    if (arguments.empty()) {
        throw std::invalid_argument("command argument list must not be empty");
    }
    int pipe_fds[2] = {-1, -1};
    if (::pipe2(pipe_fds, O_CLOEXEC) != 0) {
        throw std::system_error(errno, std::generic_category(), "pipe2");
    }
    const pid_t child = ::fork();
    if (child < 0) {
        const int error_number = errno;
        (void)::close(pipe_fds[0]);
        (void)::close(pipe_fds[1]);
        throw std::system_error(error_number, std::generic_category(), "fork");
    }
    if (child == 0) {
        (void)::close(pipe_fds[0]);
        (void)::dup2(pipe_fds[1], STDOUT_FILENO);
        (void)::close(pipe_fds[1]);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1U);
        for (const auto& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        ::execv(argv.front(), argv.data());
        ::_exit(127);
    }

    (void)::close(pipe_fds[1]);
    CommandResult result;
    char buffer[1024];
    while (true) {
        const ssize_t count = ::read(pipe_fds[0], buffer, sizeof(buffer));
        if (count > 0) {
            result.output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    (void)::close(pipe_fds[0]);
    int status = 0;
    if (::waitpid(child, &status, 0) == child && WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
    return result;
}

mw::detail::ResponseEnvelope receiveControlResponse(int fd, std::uint32_t request_id) {
    std::array<std::uint8_t, mw::detail::kControlHeaderSize> header_bytes{};
    EXPECT_EQ(mw::detail::readExact(fd, header_bytes.data(), header_bytes.size()).status,
              mw::detail::IoStatus::Complete);
    const auto header = mw::detail::decodeControlHeader(header_bytes.data(), header_bytes.size());
    EXPECT_TRUE(header.has_value());
    if (!header.has_value()) {
        return {};
    }
    EXPECT_EQ(header->opcode, mw::detail::Opcode::Response);
    EXPECT_EQ(header->request_id, request_id);
    std::vector<std::uint8_t> payload(header->payload_size);
    EXPECT_EQ(mw::detail::readExact(fd, payload.data(), payload.size()).status,
              mw::detail::IoStatus::Complete);
    const auto response = mw::detail::decodeResponsePayload(payload);
    EXPECT_TRUE(response.has_value());
    return response.value_or(mw::detail::ResponseEnvelope{});
}

TEST(RegistryDiscoveryIntegrationTest, PublisherFirstDiscoversAndTransfers) {
    const Paths paths{"publisher_first"};
    auto registry = startRegistry(paths.registry);
    ASSERT_TRUE(waitForRegistry(paths.registry));
    auto publisher = startPublisher(paths);
    ASSERT_TRUE(waitForNode(paths.registry, "ping_publisher"));
    auto subscriber = startSubscriber(paths);
    EXPECT_EQ(publisher.wait(10s), 0);
    EXPECT_EQ(subscriber.wait(10s), 0);
}

TEST(RegistryDiscoveryIntegrationTest, SubscriberFirstDiscoversAndTransfers) {
    const Paths paths{"subscriber_first"};
    auto registry = startRegistry(paths.registry);
    ASSERT_TRUE(waitForRegistry(paths.registry));
    auto subscriber = startSubscriber(paths);
    ASSERT_TRUE(waitForNode(paths.registry, "ping_subscriber"));
    auto publisher = startPublisher(paths);
    EXPECT_EQ(publisher.wait(10s), 0);
    EXPECT_EQ(subscriber.wait(10s), 0);
}

TEST(RegistryDiscoveryIntegrationTest, RejectsTypeMismatch) {
    const Paths paths{"type_mismatch"};
    auto registry = startRegistry(paths.registry);
    ASSERT_TRUE(waitForRegistry(paths.registry));
    mw::Context subscriber_context{"mismatch_subscriber", mw::RegistryConfig{paths.registry}};
    mw::SubscriberConfig subscriber_config{paths.data, 1024U, "Example", "hash-a"};
    auto subscriber = subscriber_context.createSubscriber("/typed", subscriber_config);

    mw::Context publisher_context{"mismatch_publisher", mw::RegistryConfig{paths.registry}};
    mw::PublisherConfig publisher_config{"", 1024U, "Example", "hash-b"};
    try {
        auto publisher = publisher_context.createPublisher("/typed", publisher_config);
        (void)publisher;
        FAIL() << "incompatible publisher was accepted";
    } catch (const mw::MiddlewareError& error) {
        EXPECT_EQ(error.code(), mw::ErrorCode::TypeMismatch);
    }
}

TEST(RegistryDiscoveryIntegrationTest, MwctlQueriesLiveRegistry) {
    const Paths paths{"mwctl"};
    auto registry = startRegistry(paths.registry);
    ASSERT_TRUE(waitForRegistry(paths.registry));
    mw::Context subscriber_context{"camera_subscriber", mw::RegistryConfig{paths.registry}};
    auto subscriber = subscriber_context.createSubscriber(
        "/camera/image", mw::SubscriberConfig{paths.data, 4096U, "Image", "image-v1"});
    mw::Context publisher_context{"camera_publisher", mw::RegistryConfig{paths.registry}};
    auto publisher = publisher_context.createPublisher(
        "/camera/image", mw::PublisherConfig{"", 4096U, "Image", "image-v1"});

    const auto nodes = runAndCapture({MW_MWCTL_PATH, "--registry", paths.registry, "node", "list"});
    EXPECT_EQ(nodes.exit_code, 0);
    EXPECT_NE(nodes.output.find("camera_publisher"), std::string::npos);
    EXPECT_NE(nodes.output.find("camera_subscriber"), std::string::npos);

    const auto topics =
        runAndCapture({MW_MWCTL_PATH, "--registry", paths.registry, "topic", "list"});
    EXPECT_EQ(topics.exit_code, 0);
    EXPECT_NE(topics.output.find("/camera/image"), std::string::npos);

    const auto info = runAndCapture(
        {MW_MWCTL_PATH, "--registry", paths.registry, "topic", "info", "/camera/image"});
    EXPECT_EQ(info.exit_code, 0);
    EXPECT_NE(info.output.find("type_name: Image"), std::string::npos);
    EXPECT_NE(info.output.find("type_hash: image-v1"), std::string::npos);
    EXPECT_NE(info.output.find("publishers: 1"), std::string::npos);
    EXPECT_NE(info.output.find("subscribers: 1"), std::string::npos);
}

TEST(RegistryDiscoveryIntegrationTest, RejectsMalformedControlFramesWithoutStateMutation) {
    const Paths paths{"malformed"};
    auto registry = startRegistry(paths.registry);
    ASSERT_TRUE(waitForRegistry(paths.registry));

    {
        auto socket = mw::detail::connectUnixSocket(paths.registry);
        const auto bytes = mw::detail::encodeControlFrame(mw::detail::Opcode::ListNodes, 9U, {});
        ASSERT_EQ(mw::detail::writeAll(socket.get(), bytes.data(), 5U).status,
                  mw::detail::IoStatus::Complete);
        std::this_thread::sleep_for(10ms);
        ASSERT_EQ(mw::detail::writeAll(socket.get(), bytes.data() + 5U, bytes.size() - 5U).status,
                  mw::detail::IoStatus::Complete);
        EXPECT_EQ(receiveControlResponse(socket.get(), 9U).error, mw::ErrorCode::Ok);
    }
    {
        auto socket = mw::detail::connectUnixSocket(paths.registry);
        mw::detail::PayloadWriter payload;
        payload.writeString("/missing");
        const auto bytes =
            mw::detail::encodeControlFrame(mw::detail::Opcode::QueryTopic, 90U, payload.data());
        const std::size_t first_part = mw::detail::kControlHeaderSize + 2U;
        ASSERT_EQ(mw::detail::writeAll(socket.get(), bytes.data(), first_part).status,
                  mw::detail::IoStatus::Complete);
        std::this_thread::sleep_for(10ms);
        ASSERT_EQ(
            mw::detail::writeAll(socket.get(), bytes.data() + first_part, bytes.size() - first_part)
                .status,
            mw::detail::IoStatus::Complete);
        EXPECT_EQ(receiveControlResponse(socket.get(), 90U).error, mw::ErrorCode::TopicNotFound);
    }
    {
        auto socket = mw::detail::connectUnixSocket(paths.registry);
        const auto bytes = mw::detail::encodeControlFrame(mw::detail::Opcode::ListNodes, 10U, {});
        ASSERT_EQ(mw::detail::writeAll(socket.get(), bytes.data(), bytes.size()).status,
                  mw::detail::IoStatus::Complete);
        ASSERT_EQ(::shutdown(socket.get(), SHUT_WR), 0);
        EXPECT_EQ(receiveControlResponse(socket.get(), 10U).error, mw::ErrorCode::Ok);
    }
    {
        auto socket = mw::detail::connectUnixSocket(paths.registry);
        mw::detail::ControlHeader header;
        header.magic = 0U;
        header.opcode = mw::detail::Opcode::ListNodes;
        header.request_id = 11U;
        const auto bytes = mw::detail::encodeControlHeader(header);
        ASSERT_EQ(mw::detail::writeAll(socket.get(), bytes.data(), bytes.size()).status,
                  mw::detail::IoStatus::Complete);
        EXPECT_EQ(receiveControlResponse(socket.get(), 11U).error,
                  mw::ErrorCode::InvalidControlMessage);
    }
    {
        auto socket = mw::detail::connectUnixSocket(paths.registry);
        mw::detail::ControlHeader header;
        header.version = mw::detail::kControlVersion + 1U;
        header.opcode = mw::detail::Opcode::ListNodes;
        header.request_id = 12U;
        const auto bytes = mw::detail::encodeControlHeader(header);
        ASSERT_EQ(mw::detail::writeAll(socket.get(), bytes.data(), bytes.size()).status,
                  mw::detail::IoStatus::Complete);
        EXPECT_EQ(receiveControlResponse(socket.get(), 12U).error,
                  mw::ErrorCode::UnsupportedProtocolVersion);
    }
    {
        auto socket = mw::detail::connectUnixSocket(paths.registry);
        mw::detail::ControlHeader header;
        header.opcode = mw::detail::Opcode::ListNodes;
        header.request_id = 13U;
        header.payload_size = static_cast<std::uint32_t>(mw::detail::kMaxControlPayloadSize + 1U);
        const auto bytes = mw::detail::encodeControlHeader(header);
        ASSERT_EQ(mw::detail::writeAll(socket.get(), bytes.data(), bytes.size()).status,
                  mw::detail::IoStatus::Complete);
        EXPECT_EQ(receiveControlResponse(socket.get(), 13U).error, mw::ErrorCode::MessageTooLarge);
    }
    {
        auto socket = mw::detail::connectUnixSocket(paths.registry);
        const auto bytes =
            mw::detail::encodeControlFrame(static_cast<mw::detail::Opcode>(999U), 14U, {});
        ASSERT_EQ(mw::detail::writeAll(socket.get(), bytes.data(), bytes.size()).status,
                  mw::detail::IoStatus::Complete);
        EXPECT_EQ(receiveControlResponse(socket.get(), 14U).error, mw::ErrorCode::UnknownOpcode);
    }
    {
        auto socket = mw::detail::connectUnixSocket(paths.registry);
        mw::detail::PayloadWriter payload;
        payload.writeString("partial_node");
        const auto bytes =
            mw::detail::encodeControlFrame(mw::detail::Opcode::RegisterNode, 15U, payload.data());
        ASSERT_GT(bytes.size(), 1U);
        ASSERT_EQ(mw::detail::writeAll(socket.get(), bytes.data(), bytes.size() - 1U).status,
                  mw::detail::IoStatus::Complete);
    }
    {
        auto socket = mw::detail::connectUnixSocket(paths.registry);
        const std::vector<std::uint8_t> invalid_payload{0U, 0U, 0U, 8U, 'x'};
        const auto bytes =
            mw::detail::encodeControlFrame(mw::detail::Opcode::RegisterNode, 16U, invalid_payload);
        ASSERT_EQ(mw::detail::writeAll(socket.get(), bytes.data(), bytes.size()).status,
                  mw::detail::IoStatus::Complete);
        EXPECT_EQ(receiveControlResponse(socket.get(), 16U).error,
                  mw::ErrorCode::InvalidControlMessage);
    }

    mw::detail::RegistryClient client{mw::RegistryConfig{paths.registry}};
    EXPECT_TRUE(client.listNodes().empty());
    EXPECT_TRUE(client.listTopics().empty());
}

} // namespace
