add_library(mw_benchmark_common STATIC benchmark_common.cpp)
target_compile_features(mw_benchmark_common PUBLIC cxx_std_17)
set_target_properties(mw_benchmark_common PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(
    mw_benchmark_common
    PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
)
target_include_directories(mw_benchmark_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

foreach(target mw_bench_publisher mw_bench_subscriber)
    add_executable(${target} ${target}.cpp)
    target_compile_features(${target} PRIVATE cxx_std_17)
    set_target_properties(
        ${target}
        PROPERTIES
            CXX_EXTENSIONS OFF
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    target_compile_options(
        ${target}
        PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
    )
    target_link_libraries(${target} PRIVATE mw::mw_core mw_benchmark_common)
endforeach()
