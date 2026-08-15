#include <mw/context.hpp>

#include "detail/socket_io.hpp"
#include "detail/unique_fd.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

using namespace std::chrono_literals;

std::vector<std::uint8_t> makePayload(std::size_t size, std::uint64_t sequence) {
    std::vector<std::uint8_t> payload(size);
    for (std::size_t offset = 0; offset < size; ++offset) {
        payload[offset] = static_cast<std::uint8_t>((sequence + offset * 31U) & 0xFFU);
    }
    return payload;
}

int runSubscriberChild(const std::string& path, int ready_fd) {
    try {
        mw::Context context{"process_subscriber"};
        auto subscriber = context.createSubscriber("/process", {path, 1024});
        const std::uint8_t ready = 1;
        if (mw::detail::writeAll(ready_fd, &ready, 1).status != mw::detail::IoStatus::Complete) {
            return 10;
        }

        constexpr std::uint64_t message_count = 64;
        for (std::uint64_t sequence = 1; sequence <= message_count; ++sequence) {
            auto message = subscriber.waitAndTake(5s);
            if (!message.has_value() || message->sequence != sequence ||
                message->payload != makePayload(128, sequence)) {
                return 11;
            }
        }
        return 0;
    } catch (...) {
        return 12;
    }
}

TEST(ProcessPubSubIntegrationTest, TransfersMessagesAcrossProcesses) {
    constexpr std::uint64_t message_count = 64;
    const std::string path = "/tmp/mw_phase1_process_" + std::to_string(::getpid()) + ".sock";
    int ready_descriptors[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ready_descriptors), 0);
    mw::detail::UniqueFd parent_ready{ready_descriptors[0]};
    mw::detail::UniqueFd child_ready{ready_descriptors[1]};

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        parent_ready.reset();
        const int exit_code = runSubscriberChild(path, child_ready.get());
        ::_exit(exit_code);
    }

    child_ready.reset();
    std::uint8_t ready = 0;
    ASSERT_EQ(mw::detail::readExact(parent_ready.get(), &ready, 1).status,
              mw::detail::IoStatus::Complete);
    ASSERT_EQ(ready, 1U);

    mw::Context context{"process_publisher"};
    auto publisher = context.createPublisher("/process", {path, 1024});
    for (std::uint64_t sequence = 1; sequence <= message_count; ++sequence) {
        const auto payload = makePayload(128, sequence);
        const auto result = publisher.publish(payload.data(), payload.size());
        ASSERT_TRUE(result);
        ASSERT_EQ(result.sequence, sequence);
    }

    int child_status = 0;
    ASSERT_EQ(::waitpid(child, &child_status, 0), child);
    ASSERT_TRUE(WIFEXITED(child_status));
    EXPECT_EQ(WEXITSTATUS(child_status), 0);
}

} // namespace
