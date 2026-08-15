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

## Current Status: Phase 1 - Unix Domain Socket Pub/Sub Baseline

Phase 1 provides a functional, copied-payload UDS baseline for one publisher, one subscriber, and
one explicitly configured topic on one Linux host. It adds the public Context/Publisher/Subscriber
API, a fixed frame protocol, per-publisher sequence numbers, monotonic publish timestamps, bounded
payload validation, timeout waits, clean disconnect handling, and subscriber-side reconnect.

## Architecture Direction

The final architecture separates a Unix Domain Socket control plane from a shared-memory data plane.
The Phase 1 UDS path is a deliberately simple data-plane baseline; it does not implement the later
registry control plane or shared-memory transport. The core library remains independent of ROS2.

## Implemented

- C++17 `mw_core` shared library with install/export packaging
- `Context`, move-only `Publisher`, and move-only `Subscriber`
- Explicit Unix socket path configuration
- One publisher / one subscriber / one topic UDS transport
- Fixed 24-byte frame header plus copied payload
- Strict sequence and monotonic publish timestamp metadata
- Empty, small, and large payload support up to the configured bound
- Partial read/write and `EINTR` handling
- Timeout, invalid-frame, disconnect, and subscriber reconnect behavior
- Unit, in-process integration, and cross-process integration tests

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

## UDS Quick Start

Start the subscriber:

```bash
./build/bin/mw_ping_subscriber \
  --socket /tmp/mw_phase1.sock \
  --count 10 \
  --size 64
```

In another terminal, run the publisher:

```bash
./build/bin/mw_ping_publisher \
  --socket /tmp/mw_phase1.sock \
  --count 10 \
  --size 64
```

See [docs/UDS_BASELINE.md](docs/UDS_BASELINE.md) for the wire format, ownership, and failure model.

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

- The Phase 1 baseline supports only one publisher, one subscriber, and one explicitly configured
  topic; registry-based discovery is not implemented.
- No shared memory, memory pool, subscriber queue, backpressure, or loaned-sample API exists yet.
- No heartbeat, crash recovery, ROS2 adapter, benchmark framework, or proven performance result
  exists yet.
- The UDS path copies payload data through kernel socket buffers and is not zero-copy.
- An unclean subscriber exit can leave a stale socket pathname that must be removed manually.
