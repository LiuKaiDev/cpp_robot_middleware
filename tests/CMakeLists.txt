find_package(GTest 1.14 REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

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

    gtest_discover_tests(${target} PROPERTIES TIMEOUT 20 RESOURCE_LOCK mw_project_ipc)
endfunction()

include(GoogleTest)

mw_add_gtest(mw_version_test version_test.cpp)
mw_add_gtest(mw_frame_protocol_test unit/frame_protocol_test.cpp)
mw_add_gtest(mw_control_protocol_test unit/control_protocol_test.cpp)
mw_add_gtest(mw_registry_state_test unit/registry_state_test.cpp mw_registry_support)
mw_add_gtest(mw_registry_liveness_test unit/registry_liveness_test.cpp mw_registry_support)
mw_add_gtest(mw_socket_io_test unit/socket_io_test.cpp)
mw_add_gtest(mw_shared_memory_test unit/shared_memory_test.cpp)
mw_add_gtest(mw_shm_protocol_test unit/shm_protocol_test.cpp)
mw_add_gtest(mw_pool_protocol_test unit/pool_protocol_test.cpp)
mw_add_gtest(mw_memory_pool_test unit/memory_pool_test.cpp)
mw_add_gtest(mw_subscriber_queue_test unit/subscriber_queue_test.cpp Threads::Threads)
mw_add_gtest(mw_benchmark_common_test unit/benchmark_common_test.cpp mw_benchmark_common)
mw_add_gtest(mw_pubsub_test integration/pubsub_test.cpp Threads::Threads)
mw_add_gtest(mw_process_pubsub_test integration/process_pubsub_test.cpp)
mw_add_gtest(mw_registry_discovery_test integration/registry_discovery_test.cpp
             mw_registry_support Threads::Threads)
target_compile_definitions(
    mw_registry_discovery_test
    PRIVATE
        MW_REGISTRYD_PATH="$<TARGET_FILE:mw_registryd>"
        MW_MWCTL_PATH="$<TARGET_FILE:mwctl>"
        MW_PING_PUBLISHER_PATH="$<TARGET_FILE:mw_ping_publisher>"
        MW_PING_SUBSCRIBER_PATH="$<TARGET_FILE:mw_ping_subscriber>"
)
mw_add_gtest(mw_shm_transport_test integration/shm_transport_test.cpp)
target_compile_definitions(
    mw_shm_transport_test
    PRIVATE
        MW_REGISTRYD_PATH="$<TARGET_FILE:mw_registryd>"
        MW_PING_PUBLISHER_PATH="$<TARGET_FILE:mw_ping_publisher>"
        MW_PING_SUBSCRIBER_PATH="$<TARGET_FILE:mw_ping_subscriber>"
)
add_dependencies(
    mw_shm_transport_test
    mw_registryd
    mw_ping_publisher
    mw_ping_subscriber
)
mw_add_gtest(mw_multi_subscriber_shm_test integration/multi_subscriber_shm_test.cpp)
target_compile_definitions(
    mw_multi_subscriber_shm_test
    PRIVATE
        MW_REGISTRYD_PATH="$<TARGET_FILE:mw_registryd>"
        MW_PING_PUBLISHER_PATH="$<TARGET_FILE:mw_ping_publisher>"
        MW_PING_SUBSCRIBER_PATH="$<TARGET_FILE:mw_ping_subscriber>"
)
add_dependencies(
    mw_multi_subscriber_shm_test
    mw_registryd
    mw_ping_publisher
    mw_ping_subscriber
)
mw_add_gtest(mw_loan_queue_test integration/loan_queue_integration_test.cpp
             mw_registry_support Threads::Threads)
mw_add_gtest(mw_crash_recovery_test integration/crash_recovery_integration_test.cpp
             Threads::Threads)
mw_add_gtest(mw_uds_multi_subscriber_test integration/uds_multi_subscriber_test.cpp
             mw_registry_support Threads::Threads)
target_compile_definitions(
    mw_crash_recovery_test
    PRIVATE MW_REGISTRYD_PATH="$<TARGET_FILE:mw_registryd>"
)
add_dependencies(mw_crash_recovery_test mw_registryd)

add_test(
    NAME mw_benchmark_python_test
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
            -s "${PROJECT_SOURCE_DIR}/benchmark/python/tests" -p "test_*.py"
)
set_tests_properties(
    mw_benchmark_python_test
    PROPERTIES
        TIMEOUT 30
        RESOURCE_LOCK mw_project_ipc
        ENVIRONMENT "PYTHONPATH=${PROJECT_SOURCE_DIR}/benchmark/python"
)
add_dependencies(
    mw_registry_discovery_test
    mw_registryd
    mwctl
    mw_ping_publisher
    mw_ping_subscriber
)
