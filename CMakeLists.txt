cmake_minimum_required(VERSION 3.16)

project(
    mw
    VERSION 0.1.0
    DESCRIPTION "Linux C++ middleware core"
    LANGUAGES CXX
)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_ASAN OR ENABLE_UBSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "The sanitizer options require GCC or Clang")
    endif()

    set(MW_SANITIZER_FLAGS "")
    if(ENABLE_ASAN)
        list(APPEND MW_SANITIZER_FLAGS -fsanitize=address)
    endif()
    if(ENABLE_UBSAN)
        list(APPEND MW_SANITIZER_FLAGS -fsanitize=undefined)
    endif()

    add_compile_options(${MW_SANITIZER_FLAGS} -fno-omit-frame-pointer)
    add_link_options(${MW_SANITIZER_FLAGS})
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory(middleware)
add_subdirectory(registry)
add_subdirectory(tools/mwctl)
add_subdirectory(examples)
add_subdirectory(benchmark/cpp)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(MW_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/mw")

configure_package_config_file(
    cmake/mwConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/mwConfig.cmake"
    INSTALL_DESTINATION "${MW_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/mwConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(
    EXPORT mwTargets
    FILE mwTargets.cmake
    NAMESPACE mw::
    DESTINATION "${MW_INSTALL_CMAKEDIR}"
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/mwConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/mwConfigVersion.cmake"
    DESTINATION "${MW_INSTALL_CMAKEDIR}"
)
