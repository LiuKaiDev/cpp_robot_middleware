add_executable(mwctl main.cpp)

target_compile_features(mwctl PRIVATE cxx_std_17)
set_target_properties(
    mwctl
    PROPERTIES
        CXX_EXTENSIONS OFF
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(mwctl PRIVATE "${PROJECT_SOURCE_DIR}/middleware/src")
target_link_libraries(mwctl PRIVATE mw::mw_core)
target_compile_options(
    mwctl
    PRIVATE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
)

install(TARGETS mwctl RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
