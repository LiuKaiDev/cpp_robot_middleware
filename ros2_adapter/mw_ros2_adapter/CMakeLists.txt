cmake_minimum_required(VERSION 3.16)

project(mw_ros2_adapter VERSION 0.1.0 LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(mw CONFIG REQUIRED)

add_library(
  mw_ros2_adapter_lib
  src/bridge_config.cpp
  src/bridge_nodes.cpp
  src/type_support.cpp
)
add_library(mw_ros2_adapter::mw_ros2_adapter_lib ALIAS mw_ros2_adapter_lib)

target_compile_features(mw_ros2_adapter_lib PUBLIC cxx_std_17)
set_target_properties(
  mw_ros2_adapter_lib
  PROPERTIES
    CXX_EXTENSIONS OFF
    INSTALL_RPATH_USE_LINK_PATH TRUE
)
target_compile_options(
  mw_ros2_adapter_lib
  PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
)
target_include_directories(
  mw_ros2_adapter_lib
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)
ament_target_dependencies(
  mw_ros2_adapter_lib
  rclcpp
  std_msgs
  geometry_msgs
  sensor_msgs
)
target_link_libraries(mw_ros2_adapter_lib mw::mw_core)

add_executable(ros2_to_mw_bridge src/ros2_to_mw_bridge.cpp)
add_executable(mw_to_ros2_bridge src/mw_to_ros2_bridge.cpp)
foreach(target ros2_to_mw_bridge mw_to_ros2_bridge)
  target_compile_features(${target} PRIVATE cxx_std_17)
  set_target_properties(
    ${target}
    PROPERTIES
      CXX_EXTENSIONS OFF
      INSTALL_RPATH_USE_LINK_PATH TRUE
  )
  target_compile_options(
    ${target}
    PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
  )
  target_link_libraries(${target} PRIVATE mw_ros2_adapter_lib)
endforeach()

install(
  TARGETS mw_ros2_adapter_lib
  EXPORT export_mw_ros2_adapter
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)
install(
  TARGETS ros2_to_mw_bridge mw_to_ros2_bridge
  DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

ament_export_targets(export_mw_ros2_adapter HAS_LIBRARY_TARGET)
ament_export_dependencies(rclcpp std_msgs geometry_msgs sensor_msgs mw)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  add_executable(mw_ros2_adapter_test_peer test/test_peer.cpp)
  target_compile_features(mw_ros2_adapter_test_peer PRIVATE cxx_std_17)
  set_target_properties(
    mw_ros2_adapter_test_peer
    PROPERTIES
      CXX_EXTENSIONS OFF
      INSTALL_RPATH_USE_LINK_PATH TRUE
  )
  target_compile_options(
    mw_ros2_adapter_test_peer
    PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
  )
  target_include_directories(mw_ros2_adapter_test_peer PRIVATE test)
  target_link_libraries(mw_ros2_adapter_test_peer PRIVATE mw_ros2_adapter_lib)

  ament_add_gtest(mw_ros2_adapter_codec_test test/message_codec_test.cpp)
  target_compile_features(mw_ros2_adapter_codec_test PRIVATE cxx_std_17)
  set_target_properties(mw_ros2_adapter_codec_test PROPERTIES CXX_EXTENSIONS OFF)
  target_compile_options(
    mw_ros2_adapter_codec_test
    PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
  )
  target_link_libraries(mw_ros2_adapter_codec_test mw_ros2_adapter_lib)

  get_filename_component(MW_CORE_INSTALL_PREFIX "${mw_DIR}/../../.." ABSOLUTE)
  set(MW_REGISTRYD_EXECUTABLE "${MW_CORE_INSTALL_PREFIX}/bin/mw_registryd")
  set(MW_MWCTL_EXECUTABLE "${MW_CORE_INSTALL_PREFIX}/bin/mwctl")
  if(NOT EXISTS "${MW_REGISTRYD_EXECUTABLE}" OR NOT EXISTS "${MW_MWCTL_EXECUTABLE}")
    message(FATAL_ERROR "The installed mw package must include mw_registryd and mwctl")
  endif()
  find_program(MW_ROS2_CLI_EXECUTABLE ros2 REQUIRED)

  ament_add_gtest(
    mw_ros2_adapter_integration_test
    test/bridge_integration_test.cpp
    TIMEOUT 300
  )
  target_compile_features(mw_ros2_adapter_integration_test PRIVATE cxx_std_17)
  set_target_properties(mw_ros2_adapter_integration_test PROPERTIES CXX_EXTENSIONS OFF)
  target_compile_options(
    mw_ros2_adapter_integration_test
    PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
  )
  target_compile_definitions(
    mw_ros2_adapter_integration_test
    PRIVATE
      MW_REGISTRYD_PATH="${MW_REGISTRYD_EXECUTABLE}"
      MW_MWCTL_PATH="${MW_MWCTL_EXECUTABLE}"
      MW_ROS2_TO_MW_PATH="$<TARGET_FILE:ros2_to_mw_bridge>"
      MW_MW_TO_ROS2_PATH="$<TARGET_FILE:mw_to_ros2_bridge>"
      MW_TEST_PEER_PATH="$<TARGET_FILE:mw_ros2_adapter_test_peer>"
      MW_ROS2_CLI_PATH="${MW_ROS2_CLI_EXECUTABLE}"
  )
  target_link_libraries(mw_ros2_adapter_integration_test mw_ros2_adapter_lib)
  add_dependencies(
    mw_ros2_adapter_integration_test
    ros2_to_mw_bridge
    mw_to_ros2_bridge
    mw_ros2_adapter_test_peer
  )
endif()

ament_package()
