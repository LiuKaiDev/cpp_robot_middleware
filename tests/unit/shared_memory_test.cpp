#include "detail/shared_memory.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <string>
#include <system_error>
#include <unistd.h>

namespace {

std::string regionName(const std::string& label) {
    static unsigned counter = 0;
    return "/mw_p3_test_" + std::to_string(::getpid()) + "_" + label + "_" +
           std::to_string(counter++);
}

bool isMissing(const std::string& name) {
    try {
        auto region = mw::detail::SharedMemoryRegion::openReadOnly(name, 4096U);
        (void)region;
        return false;
    } catch (const std::system_error& error) {
        return error.code().value() == ENOENT;
    }
}

TEST(SharedMemoryRegionTest, SharesDataAcrossWritableAndReadOnlyMappings) {
    const std::string name = regionName("visible");
    {
        auto owner = mw::detail::SharedMemoryRegion::create(name, 4096U);
        ASSERT_NE(owner.data(), nullptr);
        ASSERT_EQ(owner.size(), 4096U);
        ASSERT_TRUE(owner.ownsName());
        static_cast<std::uint8_t*>(owner.data())[123] = 0xA5U;

        auto reader = mw::detail::SharedMemoryRegion::openReadOnly(name, 4096U);
        EXPECT_FALSE(reader.ownsName());
        EXPECT_EQ(static_cast<const std::uint8_t*>(reader.data())[123], 0xA5U);
        EXPECT_TRUE(owner.unlinkName());
        EXPECT_TRUE(isMissing(name));
        EXPECT_EQ(static_cast<const std::uint8_t*>(reader.data())[123], 0xA5U);
    }
    EXPECT_TRUE(isMissing(name));
}

TEST(SharedMemoryRegionTest, MoveOperationsTransferMappingAndNameOwnership) {
    const std::string first_name = regionName("move_first");
    const std::string second_name = regionName("move_second");
    {
        auto first = mw::detail::SharedMemoryRegion::create(first_name, 4096U);
        static_cast<std::uint8_t*>(first.data())[0] = 17U;
        auto moved = std::move(first);
        EXPECT_EQ(first.data(), nullptr);
        EXPECT_FALSE(first.ownsName());
        EXPECT_EQ(static_cast<std::uint8_t*>(moved.data())[0], 17U);

        auto second = mw::detail::SharedMemoryRegion::create(second_name, 4096U);
        second = std::move(moved);
        EXPECT_EQ(moved.data(), nullptr);
        EXPECT_TRUE(second.ownsName());
        EXPECT_TRUE(isMissing(second_name));
    }
    EXPECT_TRUE(isMissing(first_name));
    EXPECT_TRUE(isMissing(second_name));
}

TEST(SharedMemoryRegionTest, RejectsInvalidNamesSizesAndMissingObjects) {
    EXPECT_THROW(mw::detail::SharedMemoryRegion::create("missing_slash", 4096U),
                 std::invalid_argument);
    EXPECT_THROW(mw::detail::SharedMemoryRegion::create("/has/slash", 4096U),
                 std::invalid_argument);
    EXPECT_THROW(mw::detail::SharedMemoryRegion::create("/valid_but_zero", 0U),
                 std::invalid_argument);
    EXPECT_THROW(mw::detail::SharedMemoryRegion::openReadOnly(regionName("missing"), 4096U),
                 std::system_error);
}

TEST(SharedMemoryRegionTest, RejectsUnexpectedObjectSizeAndCleansUpOnDestruction) {
    const std::string name = regionName("size");
    {
        auto owner = mw::detail::SharedMemoryRegion::create(name, 4096U);
        EXPECT_THROW(mw::detail::SharedMemoryRegion::openReadOnly(name, 2048U), std::system_error);
    }
    EXPECT_TRUE(isMissing(name));
}

} // namespace
