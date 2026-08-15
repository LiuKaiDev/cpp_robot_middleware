# cpp_robot_middleware

## Project Overview

`cpp_robot_middleware` is a Linux and C++17 project for building a small, explainable,
testable, and benchmarkable local multi-process publish/subscribe middleware for robotics.
The middleware core is designed as an independent C++ library rather than a ROS2 component.

## Why This Project

Robot applications exchange both small control messages and large sensor payloads. This project
provides a focused environment for studying the IPC, resource-lifetime, backpressure, and
performance tradeoffs beneath a robotics communication API without implementing a full DDS or
ROS2 RMW stack.

## Current Status: Phase 0

Phase 0 provides the C++17 project skeleton, the shared `mw_core` library, a minimal version API,
CTest integration, install/export packaging, and an external package consumer. No publish/subscribe
transport is implemented yet.

## Architecture Direction

The planned architecture separates a Unix Domain Socket control plane from a shared-memory data
plane. These runtime components are architectural direction only and are not implemented in Phase
0. The core library will remain independent of ROS2; a later adapter layer will provide ROS2
integration.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

The build produces `libmw_core.so`.

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Install

```bash
cmake --install build --prefix "$PWD/_install"
```

The installed CMake package exports the shared library as `mw::mw_core` and installs public headers
under `include/mw`.

## External Consumer

The standalone example consumes only the installed package:

```bash
cmake -S examples/external_consumer -B build_external \
    -DCMAKE_PREFIX_PATH="$PWD/_install"
cmake --build build_external -j
./build_external/mw_external_consumer
```

An external CMake project uses the package with:

```cmake
find_package(mw CONFIG REQUIRED)
target_link_libraries(example PRIVATE mw::mw_core)
```

## Development Phases

- Phase 0: Project skeleton and CMake library packaging.
- Phase 1: Unix Domain Socket publish/subscribe baseline.
- Phase 2: Registry, discovery, and `mwctl`.
- Phase 3: Shared-memory data plane V1.
- Phase 4: Memory pool, chunk lifecycle, and multiple subscribers.
- Phase 5: Ring buffer, backpressure, and loaned samples.
- Phase 6: Heartbeat, crash recovery, and resource cleanup.
- Phase 7: ROS2 adapter.
- Phase 8: Benchmarking, profiling, and evidence-based optimization.
- Phase 9: Final documentation and demos.

## Known Limitations

- No Publisher or Subscriber API exists yet.
- No UDS transport, registry, discovery, or command-line tooling exists yet.
- No shared-memory data plane or runtime resource management exists yet.
- No ROS2 adapter or benchmark results exist yet.
- Version 1 will target one active publisher per topic and multiple subscribers on one Linux host.
