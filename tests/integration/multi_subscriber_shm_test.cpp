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
#include <fcntl.h>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
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
    ChildProcess() = default;
    explicit ChildProcess(pid_t process, int output_fd = -1) noexcept
        : process_(process), output_fd_(output_fd) {}
    ~ChildProcess() { terminate(); }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept
        : process_(std::exchange(other.process_, -1)),
          output_fd_(std::exchange(other.output_fd_, -1)) {}
    ChildProcess& operator=(ChildProcess&& other) noexcept {
        if (this != &other) {
            terminate();
            process_ = std::exchange(other.process_, -1);
            output_fd_ = std::exchange(other.output_fd_, -1);
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
                readOutput();
                return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
            if (result < 0) {
                process_ = -1;
                readOutput();
                return 255;
            }
            std::this_thread::sleep_for(10ms);
        }
        return 254;
    }

    const std::string& output() const noexcept { return output_; }

  private:
    void readOutput() noexcept {
        if (output_fd_ < 0) {
            return;
        }
        char buffer[512];
        while (true) {
            const ssize_t count = ::read(output_fd_, buffer, sizeof(buffer));
            if (count > 0) {
                output_.append(buffer, static_cast<std::size_t>(count));
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        (void)::close(output_fd_);
        output_fd_ = -1;
    }

    void terminate() noexcept {
        if (process_ > 0) {
            (void)::kill(process_, SIGTERM);
            (void)::waitpid(process_, nullptr, 0);
            process_ = -1;
        }
        readOutput();
    }

    pid_t process_{-1};
    int output_fd_{-1};
    std::string output_;
};

ChildProcess spawn(const std::vector<std::string>& arguments, bool capture_output) {
    int pipe_fds[2] = {-1, -1};
    if (capture_output && ::pipe2(pipe_fds, O_CLOEXEC) != 0) {
        throw std::system_error(errno, std::generic_category(), "pipe2");
    }
    const pid_t child = ::fork();
    if (child < 0) {
        throw std::system_error(errno, std::generic_category(), "fork");
    }
    if (child == 0) {
        if (capture_output) {
            (void)::close(pipe_fds[0]);
            (void)::dup2(pipe_fds[1], STDOUT_FILENO);
            (void)::dup2(pipe_fds[1], STDERR_FILENO);
            (void)::close(pipe_fds[1]);
        } else {
            const int null_fd = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (null_fd >= 0) {
                (void)::dup2(null_fd, STDOUT_FILENO);
                (void)::dup2(null_fd, STDERR_FILENO);
                (void)::close(null_fd);
            }
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
    if (capture_output) {
        (void)::close(pipe_fds[1]);
    }
    return ChildProcess{child, capture_output ? pipe_fds[0] : -1};
}

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

bool waitForSubscriberCount(const std::string& path, std::size_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            mw::detail::RegistryClient client{mw::RegistryConfig{path}};
            if (client.queryTopic("/phase4/multi").subscriber_count == expected) {
                return true;
            }
        } catch (...) {
        }
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

bool waitForPublisherPool(const std::string& path, const std::string& topic) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            mw::detail::RegistryClient client{mw::RegistryConfig{path}};
            const auto info = client.queryTopic(topic);
            if (info.publisher_count == 1U && info.pool.pool_id != 0U &&
                !info.pool.shm_name.empty() && info.pool.segment_size != 0U &&
                info.pool.layout_version == mw::detail::kPoolLayoutVersion) {
                return true;
            }
        } catch (...) {
        }
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

std::set<std::string> projectPoolObjects() {
    std::set<std::string> objects;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator{"/dev/shm", error}, end;
         !error && iterator != end; iterator.increment(error)) {
        const std::string name = iterator->path().filename().string();
        if (name.rfind("mw_p4_", 0U) == 0U || name.rfind("mw_p5_", 0U) == 0U ||
            name.rfind("mw_q5_", 0U) == 0U) {
            objects.insert(name);
        }
    }
    return objects;
}

struct LogicalChunk {
    std::uint64_t pool_id{0};
    std::uint64_t chunk_index{0};
    std::uint64_t generation{0};
    std::uint64_t payload_offset{0};

    bool operator==(const LogicalChunk& other) const noexcept {
        return pool_id == other.pool_id && chunk_index == other.chunk_index &&
               generation == other.generation && payload_offset == other.payload_offset;
    }
};

LogicalChunk parseLogicalChunk(const std::string& output) {
    std::istringstream input{output};
    std::map<std::string, std::uint64_t> fields;
    std::string token;
    while (input >> token) {
        const std::size_t delimiter = token.find('=');
        if (delimiter != std::string::npos) {
            fields[token.substr(0U, delimiter)] = std::stoull(token.substr(delimiter + 1U));
        }
    }
    return {fields.at("pool_id"), fields.at("chunk_index"), fields.at("generation"),
            fields.at("payload_offset")};
}

TEST(MultiSubscriberShmIntegrationTest, FourProcessesShareOneLogicalChunkAndReleaseIt) {
    const auto initial_objects = projectPoolObjects();
    const std::string suffix = std::to_string(::getpid());
    const std::string registry_path = "/tmp/mw_p4_multi_registry_" + suffix + ".sock";
    ChildProcess registry = spawn({MW_REGISTRYD_PATH, "--socket", registry_path}, false);
    ASSERT_TRUE(waitForRegistry(registry_path));

    std::vector<ChildProcess> subscribers;
    subscribers.reserve(4U);
    for (std::size_t index = 0; index < 4U; ++index) {
        const std::string identity = std::to_string(index + 1U);
        subscribers.push_back(spawn({MW_PING_SUBSCRIBER_PATH, "--registry", registry_path,
                                     "--node-name", "phase4_subscriber_" + identity, "--socket",
                                     "/tmp/mw_p4_multi_data_" + suffix + "_" + identity + ".sock",
                                     "--topic", "/phase4/multi", "--transport", "shm", "--count",
                                     "2", "--size", "1024", "--timeout-ms", "10000"},
                                    true));
    }
    ASSERT_TRUE(waitForSubscriberCount(registry_path, 4U));

    ChildProcess publisher =
        spawn({MW_PING_PUBLISHER_PATH, "--registry", registry_path, "--node-name",
               "phase4_publisher", "--socket", "/tmp/mw_p4_unused.sock", "--topic", "/phase4/multi",
               "--transport", "shm", "--count", "2", "--size", "1024"},
              true);
    EXPECT_EQ(publisher.wait(15s), 0) << publisher.output();

    std::vector<LogicalChunk> identities;
    for (ChildProcess& subscriber : subscribers) {
        ASSERT_EQ(subscriber.wait(15s), 0) << subscriber.output();
        EXPECT_NE(subscriber.output().find("received=2"), std::string::npos);
        EXPECT_NE(subscriber.output().find("sequence_errors=0"), std::string::npos);
        EXPECT_NE(subscriber.output().find("payload_errors=0"), std::string::npos);
        identities.push_back(parseLogicalChunk(subscriber.output()));
    }
    ASSERT_EQ(identities.size(), 4U);
    EXPECT_NE(identities.front().pool_id, 0U);
    EXPECT_NE(identities.front().generation, 0U);
    for (const LogicalChunk& identity : identities) {
        EXPECT_EQ(identity, identities.front());
    }
    EXPECT_EQ(projectPoolObjects(), initial_objects);
}

TEST(MultiSubscriberShmIntegrationTest, PublisherFirstAdvertisesPoolBeforeWaitingForSubscriber) {
    const auto initial_objects = projectPoolObjects();
    const std::string suffix = std::to_string(::getpid());
    const std::string registry_path = "/tmp/mw_p4_first_registry_" + suffix + ".sock";
    const std::string data_path = "/tmp/mw_p4_first_data_" + suffix + ".sock";
    const std::string topic = "/phase4/publisher_first";
    ChildProcess registry = spawn({MW_REGISTRYD_PATH, "--socket", registry_path}, false);
    ASSERT_TRUE(waitForRegistry(registry_path));

    ChildProcess publisher =
        spawn({MW_PING_PUBLISHER_PATH, "--registry", registry_path, "--node-name",
               "phase4_first_publisher", "--socket", "/tmp/mw_p4_unused.sock", "--topic", topic,
               "--transport", "shm", "--count", "1", "--size", "64"},
              true);
    ASSERT_TRUE(waitForPublisherPool(registry_path, topic));

    ChildProcess subscriber =
        spawn({MW_PING_SUBSCRIBER_PATH, "--registry", registry_path, "--node-name",
               "phase4_first_subscriber", "--socket", data_path, "--topic", topic, "--transport",
               "shm", "--count", "1", "--size", "64", "--timeout-ms", "10000"},
              true);
    EXPECT_EQ(publisher.wait(15s), 0) << publisher.output();
    EXPECT_EQ(subscriber.wait(15s), 0) << subscriber.output();
    EXPECT_NE(subscriber.output().find("received=1"), std::string::npos);
    EXPECT_NE(subscriber.output().find("payload_errors=0"), std::string::npos);
    EXPECT_EQ(projectPoolObjects(), initial_objects);
}

TEST(MultiSubscriberShmIntegrationTest, NormalDisconnectReleasesKnownReferenceForReuse) {
    const auto initial_objects = projectPoolObjects();
    const std::string suffix = std::to_string(::getpid());
    const std::string registry_path = "/tmp/mw_p4_disconnect_registry_" + suffix + ".sock";
    const std::string abandoned_path = "/tmp/mw_p4_disconnect_abandoned_" + suffix + ".sock";
    const std::string replacement_path = "/tmp/mw_p4_disconnect_replacement_" + suffix + ".sock";
    const std::string topic = "/phase4/disconnect";
    ChildProcess registry = spawn({MW_REGISTRYD_PATH, "--socket", registry_path}, false);
    ASSERT_TRUE(waitForRegistry(registry_path));

    {
        mw::Context publisher_context{"disconnect_publisher", mw::RegistryConfig{registry_path}};
        mw::PublisherConfig publisher_config;
        publisher_config.max_message_size = 4096U;
        publisher_config.transport = mw::TransportType::SharedMemory;
        publisher_config.memory_pool.size_classes = {{4096U, 1U}};
        auto publisher = publisher_context.createPublisher(topic, publisher_config);
        std::vector<std::uint8_t> payload(1024U, 0x7BU);

        {
            mw::Context abandoned_context{"abandoned_subscriber",
                                          mw::RegistryConfig{registry_path}};
            mw::SubscriberConfig abandoned_config;
            abandoned_config.socket_path = abandoned_path;
            abandoned_config.max_message_size = 4096U;
            abandoned_config.transport = mw::TransportType::SharedMemory;
            auto abandoned = abandoned_context.createSubscriber(topic, abandoned_config);
            EXPECT_EQ(publisher.publish(payload.data(), payload.size()).error,
                      mw::ErrorCode::Ok);
        }

        mw::Context subscriber_context{"replacement_subscriber", mw::RegistryConfig{registry_path}};
        mw::SubscriberConfig subscriber_config;
        subscriber_config.socket_path = replacement_path;
        subscriber_config.max_message_size = 4096U;
        subscriber_config.transport = mw::TransportType::SharedMemory;
        auto subscriber = subscriber_context.createSubscriber(topic, subscriber_config);

        EXPECT_EQ(publisher.publish(payload.data(), payload.size()).error,
                  mw::ErrorCode::QueueClosed);
        const mw::PublishResult replacement_result =
            publisher.publish(payload.data(), payload.size());
        EXPECT_EQ(replacement_result.error, mw::ErrorCode::Ok);
        const auto message = subscriber.waitAndTake(5s);
        ASSERT_TRUE(message.has_value());
        EXPECT_EQ(message->payload, payload);
    }
    EXPECT_EQ(projectPoolObjects(), initial_objects);
}

} // namespace
