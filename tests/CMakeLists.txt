find_package(GTest 1.14 REQUIRED)

add_executable(mw_version_test version_test.cpp)

target_compile_features(mw_version_test PRIVATE cxx_std_17)
set_target_properties(mw_version_test PROPERTIES CXX_EXTENSIONS OFF)

target_compile_options(
    mw_version_test
    PRIVATE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
)

target_link_libraries(mw_version_test PRIVATE mw::mw_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(mw_version_test)
