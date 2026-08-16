# Phase 0 Report

## Scope

Phase 0 establishes a minimal Linux C++17 library project that configures and builds with CMake,
produces the shared `mw_core` library, exposes a public version header, integrates GoogleTest with
CTest, installs and exports a CMake package, and provides a standalone consumer of the installed
package.

No runtime middleware functionality was added.

## Files Added

- `.clang-format`
- `CMakeLists.txt`
- `README.md`
- `cmake/mwConfig.cmake.in`
- `middleware/CMakeLists.txt`
- `middleware/include/mw/version.hpp`
- `middleware/src/version.cpp`
- `tests/CMakeLists.txt`
- `tests/version_test.cpp`
- `examples/external_consumer/CMakeLists.txt`
- `examples/external_consumer/main.cpp`
- `PHASE_0_REPORT.md`

## Files Modified

None. The bootstrap `.gitignore` already covered the required generated files and directories.

## Architecture Decisions

- The project requires C++17 and disables compiler extensions on project targets.
- `mw_core` is a shared library with the build-tree alias and installed target `mw::mw_core`.
- Public build and install include interfaces both support `#include <mw/version.hpp>`.
- The only Phase 0 API is `mw::version()`, which returns `0.1.0`.
- GCC and Clang builds enable `-Wall`, `-Wextra`, and `-Wpedantic` for project targets.
- `GNUInstallDirs` and `CMakePackageConfigHelpers` provide standard, relocatable package paths.
- The external consumer is a separate CMake project and has no source-tree include or target path.

## Dependencies

- CMake 3.16 or newer; verified with CMake 3.28.3.
- A C++17 compiler; verified with GCC 13.3.0.
- GoogleTest 1.14 when `BUILD_TESTING=ON`; the system CMake package was used.

No dependency was downloaded, vendored, or added to the middleware core.

## Build Commands

The `build`, `_install`, and `build_external` paths were verified absent before configuration. The
command runner did not permit the requested `rm -rf` form, so no removal command was needed or run.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

## Build Result

Passed. CMake configured successfully, and all targets built without compiler warnings. The build
created:

```text
build/middleware/libmw_core.so
build/middleware/libmw_core.so.0
build/middleware/libmw_core.so.0.1.0
```

`file` identified `libmw_core.so` as a link to the versioned Linux shared object, whose SONAME is
`libmw_core.so.0`.

## Test Commands

```bash
ctest --test-dir build --output-on-failure
```

## Test Result

Passed: 1/1 tests.

```text
VersionTest.ReportsProjectVersion ... Passed
100% tests passed, 0 tests failed out of 1
```

The test links `mw_core`, calls the public API, checks for a non-null result, and verifies the value
`0.1.0`.

## Install Verification

Command:

```bash
cmake --install build --prefix "$PWD/_install"
```

Passed. The install tree contains:

```text
_install/include/mw/version.hpp
_install/lib/libmw_core.so
_install/lib/libmw_core.so.0
_install/lib/libmw_core.so.0.1.0
_install/lib/cmake/mw/mwConfig.cmake
_install/lib/cmake/mw/mwConfigVersion.cmake
_install/lib/cmake/mw/mwTargets.cmake
_install/lib/cmake/mw/mwTargets-debug.cmake
```

## External Consumer Verification

Commands:

```bash
cmake -S examples/external_consumer -B build_external \
    -DCMAKE_PREFIX_PATH="$PWD/_install"
cmake --build build_external -j
./build_external/mw_external_consumer
```

Passed. `find_package(mw CONFIG REQUIRED)` located the installed package, the consumer linked
against `mw::mw_core`, and execution printed:

```text
0.1.0
```

## Formatting Verification

`clang-format` 18.1.3 formatted all Phase 0 C++ files, and the following check passed:

```bash
clang-format --dry-run --Werror middleware/include/mw/version.hpp \
    middleware/src/version.cpp tests/version_test.cpp \
    examples/external_consumer/main.cpp
```

## Environment Limitations

- The command runner rejected `rm -rf -- build _install build_external`; all three paths were
  already absent, so the acceptance run still began from a clean generated state.
- No build, test, packaging, formatting, or dependency limitation was encountered.

## Known Limitations

- No Pub/Sub functionality exists yet.
- No UDS transport exists yet.
- No registry exists yet.
- No shared memory exists yet.
- No ROS2 adapter exists yet.
- No benchmark exists yet.

## Phase Boundary Confirmation

Phase 1 was not implemented.
