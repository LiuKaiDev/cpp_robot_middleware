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

## Current Status: Phase 4 - Memory Pool / Message Lifecycle

Phase 4 replaces per-message SHM objects with one preallocated publisher-lifetime pool. A publisher
stores a payload once in a generation-protected chunk, sends the same logical handle to every
discovered subscriber, and reuses the chunk after all release references reach zero. The copied
Phase 1 UDS transport remains an independent baseline.

## Architecture Direction

The architecture separates the `mw_registryd` UDS control plane from publisher-to-subscriber data
transfer. Registry protocol v3 distributes pool and N-subscriber endpoint metadata without owning
payload memory. Direct mode retains the Phase 1 UDS baseline. The core library remains independent
of ROS2.

## Implemented

- C++17 `mw_core` shared library with install/export packaging
- `Context`, move-only `Publisher`, and move-only `Subscriber`
- Direct UDS mode and explicit registry-discovery mode
- `mw_registryd` single-threaded `epoll` control server
- Node registration and clean unregistration
- Topic advertisement/subscription and clean endpoint removal
- `node_id`, `topic_id`, and `endpoint_id` assignment
- Exact `type_name` plus `type_hash` compatibility and one-active-publisher enforcement
- Publisher-first and subscriber-first endpoint discovery
- `mwctl node list`, `topic list`, and `topic info`
- `TransportType::UnixDomainSocket` and `TransportType::SharedMemory`
- Registry transport compatibility checks and SHM discovery metadata
- Move-only `SharedMemoryRegion` ownership for `shm_open`, `ftruncate`, `mmap`, and `munmap`
- One preallocated POSIX SHM pool per SHM publisher lifetime
- Configurable 256 B, 4 KiB, 64 KiB, 1 MiB, and 4 MiB chunk classes
- Checked pool/class/directory layout and aligned `ChunkHeader`
- Mutex-protected publisher-owned per-class free lists
- Generation-protected logical chunk handles and explicit lifecycle state
- Publisher-owned reference decrement from exactly-once subscriber release frames
- One shared SHM payload and N fixed handle notifications for multiple subscribers
- Persistent subscriber pool mapping and publisher-owned normal-path `shm_unlink`
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

The build produces `libmw_core.so`, `mw_registryd`, `mwctl`, and the ping demos.

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Registry Quick Start

Start the registry:

```bash
./build/bin/mw_registryd
```

Start the subscriber in either order relative to the publisher:

```bash
./build/bin/mw_ping_subscriber \
  --registry /tmp/mw_registry.sock \
  --socket /tmp/mw_ping.sock \
  --transport shm \
  --count 10 \
  --size 64
```

In another terminal, run the publisher:

```bash
./build/bin/mw_ping_publisher \
  --registry /tmp/mw_registry.sock \
  --socket /tmp/mw_ping.sock \
  --transport shm \
  --count 10 \
  --size 64
```

Inspect the live registry with:

```bash
./build/bin/mwctl node list
./build/bin/mwctl topic list
./build/bin/mwctl topic info /ping
```

Use `--transport uds` for the registry-discovered copied-payload baseline; omit `--registry` for
direct UDS mode. See [docs/DATA_PLANE.md](docs/DATA_PLANE.md) for both payload paths,
[docs/CONTROL_PLANE.md](docs/CONTROL_PLANE.md) for discovery, and
[docs/MEMORY_POOL.md](docs/MEMORY_POOL.md) for the Phase 4 layout and lifecycle.

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

- Registry-discovered SHM supports N subscribers; the copied UDS baseline remains one-to-one.
- No subscriber ring buffer/queue or `DROP_NEWEST`, `DROP_OLDEST`, or blocking backpressure policy.
- No public `LoanedSample`, public loan API, or read-only `SampleView`.
- No `eventfd`, shared notification queue, or `SCM_RIGHTS` optimization.
- No heartbeat, crash detection/recovery, ROS2 adapter, or benchmark framework exists.
- Registry requests are synchronous and are not multiplexed across application threads.
- The UDS path copies payload data through kernel socket buffers and is not zero-copy.
- The SHM path copies the application buffer into one shared chunk and each subscriber copies that
  chunk into owning `ReceivedMessage`; it is not zero-copy.
- Clean completion unlinks publisher-owned pools. Crash-time orphan and refcount repair after
  `SIGKILL` are deferred to Phase 6.
- An unclean subscriber exit can leave a stale socket pathname that must be removed manually.
