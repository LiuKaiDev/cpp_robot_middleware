find_package(GTest 1.14 REQUIRED)
find_package(Threads REQUIRED)

function(mw_add_gtest target source)
    add_executable(${target} ${source})

    target_compile_features(${target} PRIVATE cxx_std_17)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    target_compile_options(
        ${target}
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
    )

    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/middleware/src")
    target_link_libraries(${target} PRIVATE mw::mw_core GTest::gtest_main ${ARGN})

    gtest_discover_tests(${target} PROPERTIES TIMEOUT 20)
endfunction()

include(GoogleTest)

mw_add_gtest(mw_version_test version_test.cpp)
mw_add_gtest(mw_frame_protocol_test unit/frame_protocol_test.cpp)
mw_add_gtest(mw_socket_io_test unit/socket_io_test.cpp)
mw_add_gtest(mw_pubsub_test integration/pubsub_test.cpp Threads::Threads)
mw_add_gtest(mw_process_pubsub_test integration/process_pubsub_test.cpp)
