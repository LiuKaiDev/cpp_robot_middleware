cmake_minimum_required(VERSION 3.16)

project(mw_external_consumer LANGUAGES CXX)

find_package(mw CONFIG REQUIRED)

add_executable(mw_external_consumer main.cpp)

target_compile_features(mw_external_consumer PRIVATE cxx_std_17)
set_target_properties(mw_external_consumer PROPERTIES CXX_EXTENSIONS OFF)

target_compile_options(
    mw_external_consumer
    PRIVATE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
)

target_link_libraries(mw_external_consumer PRIVATE mw::mw_core)
