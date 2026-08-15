function(mw_add_ping_example target source)
    add_executable(${target} ${source})
    target_compile_features(${target} PRIVATE cxx_std_17)
    set_target_properties(
        ${target}
        PROPERTIES
            CXX_EXTENSIONS OFF
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
    )
    target_compile_options(
        ${target}
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
    )
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${target} PRIVATE mw::mw_core)
endfunction()

mw_add_ping_example(mw_ping_publisher ping_publisher/main.cpp)
mw_add_ping_example(mw_ping_subscriber ping_subscriber/main.cpp)
