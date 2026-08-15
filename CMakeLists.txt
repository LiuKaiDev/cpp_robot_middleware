cmake_minimum_required(VERSION 3.16)

project(
    mw
    VERSION 0.1.0
    DESCRIPTION "Linux C++ middleware core"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory(middleware)

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
