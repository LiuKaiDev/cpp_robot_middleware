#include <mw/version.hpp>

#include <gtest/gtest.h>

#include <string_view>

TEST(VersionTest, ReportsProjectVersion) {
    ASSERT_NE(mw::version(), nullptr);
    EXPECT_EQ(std::string_view{mw::version()}, "0.1.0");
}
