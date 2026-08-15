add_library(
    mw_core SHARED
    src/context.cpp
    src/detail/control_protocol.cpp
    src/detail/frame_protocol.cpp
    src/detail/registry_client.cpp
    src/detail/socket_io.cpp
    src/detail/unix_socket.cpp
    src/publisher.cpp
    src/subscriber.cpp
    src/version.cpp
)
add_library(mw::mw_core ALIAS mw_core)

target_compile_features(mw_core PUBLIC cxx_std_17)
set_target_properties(
    mw_core
    PROPERTIES
        CXX_EXTENSIONS OFF
        VERSION "${PROJECT_VERSION}"
        SOVERSION "${PROJECT_VERSION_MAJOR}"
)

target_include_directories(
    mw_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_options(
    mw_core
    PRIVATE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
)

install(
    TARGETS mw_core
    EXPORT mwTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
