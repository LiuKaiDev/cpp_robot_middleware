cmake_minimum_required(VERSION 3.16)

project(mw_ros2_benchmark VERSION 0.1.0 LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_library(mw_ros2_benchmark_common STATIC ../../cpp/benchmark_common.cpp)
target_compile_features(mw_ros2_benchmark_common PUBLIC cxx_std_17)
set_target_properties(mw_ros2_benchmark_common PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(
  mw_ros2_benchmark_common
  PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
)
target_include_directories(mw_ros2_benchmark_common PUBLIC ../../cpp)

add_executable(mw_ros2_bench_publisher src/ros2_bench_publisher.cpp)
add_executable(mw_ros2_bench_subscriber src/ros2_bench_subscriber.cpp)
foreach(target mw_ros2_bench_publisher mw_ros2_bench_subscriber)
  target_compile_features(${target} PRIVATE cxx_std_17)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  target_compile_options(
    ${target}
    PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
  )
  ament_target_dependencies(${target} rclcpp std_msgs)
  target_link_libraries(${target} mw_ros2_benchmark_common)
endforeach()

install(
  TARGETS mw_ros2_bench_publisher mw_ros2_bench_subscriber
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
