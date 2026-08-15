add_library(
    mw_registry_support STATIC
    src/registry_server.cpp
    src/registry_state.cpp
)

target_compile_features(mw_registry_support PUBLIC cxx_std_17)
set_target_properties(mw_registry_support PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(
    mw_registry_support
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/middleware/src"
)
target_compile_options(
    mw_registry_support
    PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
)
target_link_libraries(mw_registry_support PUBLIC mw::mw_core)

add_executable(mw_registryd src/main.cpp)
target_compile_features(mw_registryd PRIVATE cxx_std_17)
set_target_properties(
    mw_registryd
    PROPERTIES
        CXX_EXTENSIONS OFF
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)
target_compile_options(
    mw_registryd
    PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
)
target_link_libraries(mw_registryd PRIVATE mw_registry_support)

install(TARGETS mw_registryd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
