#include "detail/socket_io.hpp"
#include "detail/unique_fd.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <vector>

namespace {

TEST(SocketIoTest, ReadExactHandlesEintrAndPartialReads) {
    const std::array<std::uint8_t, 9> source{0, 1, 2, 3, 4, 5, 6, 7, 8};
    std::array<std::uint8_t, source.size()> destination{};
    std::size_t source_offset = 0;
    bool interrupted = false;

    const auto result = mw::detail::readExactWith(
        destination.data(), destination.size(), [&](void* output, std::size_t remaining) {
            if (!interrupted) {
                interrupted = true;
                errno = EINTR;
                return static_cast<ssize_t>(-1);
            }
            const std::size_t chunk =
                std::min<std::size_t>({3U, remaining, source.size() - source_offset});
            std::memcpy(output, source.data() + source_offset, chunk);
            source_offset += chunk;
            return static_cast<ssize_t>(chunk);
        });

    EXPECT_EQ(result.status, mw::detail::IoStatus::Complete);
    EXPECT_EQ(result.transferred, source.size());
    EXPECT_EQ(destination, source);
}

TEST(SocketIoTest, WriteAllHandlesEintrAndPartialWrites) {
    const std::array<std::uint8_t, 7> source{9, 8, 7, 6, 5, 4, 3};
    std::vector<std::uint8_t> destination;
    bool interrupted = false;

    const auto result = mw::detail::writeAllWith(
        source.data(), source.size(), [&](const void* input, std::size_t remaining) {
            if (!interrupted) {
                interrupted = true;
                errno = EINTR;
                return static_cast<ssize_t>(-1);
            }
            const std::size_t chunk = std::min<std::size_t>(2U, remaining);
            const auto* bytes = static_cast<const std::uint8_t*>(input);
            destination.insert(destination.end(), bytes, bytes + chunk);
            return static_cast<ssize_t>(chunk);
        });

    EXPECT_EQ(result.status, mw::detail::IoStatus::Complete);
    EXPECT_EQ(result.transferred, source.size());
    EXPECT_EQ(destination, std::vector<std::uint8_t>(source.begin(), source.end()));
}

TEST(SocketIoTest, ReadExactReportsPeerDisconnectAndTransferredBytes) {
    int descriptors[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, descriptors), 0);
    mw::detail::UniqueFd reader{descriptors[0]};
    mw::detail::UniqueFd writer{descriptors[1]};

    const std::array<std::uint8_t, 3> partial{1, 2, 3};
    ASSERT_EQ(::send(writer.get(), partial.data(), partial.size(), MSG_NOSIGNAL),
              static_cast<ssize_t>(partial.size()));
    writer.reset();

    std::array<std::uint8_t, 8> destination{};
    const auto result = mw::detail::readExact(reader.get(), destination.data(), destination.size());
    EXPECT_EQ(result.status, mw::detail::IoStatus::Closed);
    EXPECT_EQ(result.transferred, partial.size());
    EXPECT_TRUE(std::equal(partial.begin(), partial.end(), destination.begin()));
}

TEST(SocketIoTest, WriteAllSuppressesSigpipeAfterPeerDisconnect) {
    int descriptors[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, descriptors), 0);
    mw::detail::UniqueFd writer{descriptors[0]};
    mw::detail::UniqueFd peer{descriptors[1]};
    peer.reset();

    const std::uint8_t byte = 42;
    const auto result = mw::detail::writeAll(writer.get(), &byte, 1);
    EXPECT_NE(result.status, mw::detail::IoStatus::Complete);
}

} // namespace
