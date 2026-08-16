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

## Current Status: Phase 7 - ROS2 Adapter

Phase 7 adds an independent bidirectional ROS2 adapter for `std_msgs/msg/String`,
`geometry_msgs/msg/Twist`, and `sensor_msgs/msg/Image`. It serializes through public `rclcpp` APIs
and bridges both UDS and SHM middleware transports. The middleware core remains ROS2-independent;
the adapter is a separate ament package that consumes installed `mw::mw_core`.

## Architecture Direction

The architecture separates the `mw_registryd` UDS control plane from publisher-to-subscriber data
transfer. Registry protocol v5 distributes pool metadata plus each subscriber's queue descriptor
without owning payload memory or queue storage. Direct mode retains the Phase 1 UDS baseline. The
core library remains independent of ROS2. The dependency direction is exclusively
`mw_ros2_adapter -> mw::mw_core`.

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
- One shared SHM payload, N bounded handle queues, and fixed metadata wakes/releases
- One subscriber-owned fixed-capacity SHM ring queue per SHM endpoint
- Per-subscriber `DROP_NEWEST`, `DROP_OLDEST`, and monotonic `BLOCK_WITH_TIMEOUT`
- Queue wraparound, empty/full handling, wake coalescing, and internal queue counters
- Publisher guard reference preventing enqueue-versus-reclaim races
- `Publisher::loan()` and move-only RAII `LoanedSample`
- Cancellation of unpublished loans, move ownership, and double-publish protection
- Move-only, read-only RAII `SampleView`
- `Subscriber::takeView()` and `waitAndTakeView()` without an owning payload copy
- Shared view release context allowing `SampleView` to outlive `Subscriber`
- Retained SHM copy path through `Publisher::publish()` and owning receive compatibility
- Persistent subscriber pool mapping and publisher-owned normal-path `shm_unlink`
- Fixed 24-byte frame header plus copied payload
- Strict sequence and monotonic publish timestamp metadata
- Empty, small, and large payload support up to the configured bound
- Partial read/write and `EINTR` handling
- Timeout, invalid-frame, disconnect, and subscriber reconnect behavior
- Unit, in-process integration, and cross-process integration tests
- Registry heartbeat thread with configurable monotonic interval/suspect/dead timeouts
- ALIVE, SUSPECTED, and terminal DEAD node state with unique session identity
- Immediate primary control EOF/HUP cleanup plus heartbeat timeout detection for open sockets
- Exact dead publisher pool and dead subscriber queue/socket cleanup through the registry
- Bounded per-endpoint outstanding handle tracking and exactly-once release repair
- Robust process-shared queue mutex owner-death recovery and blocked-producer wakeup
- Publisher and subscriber reconnect after peer `SIGKILL`, including replacement endpoints
- Independent `mw_ros2_adapter` ament package consuming the installed core package
- `ros2_to_mw_bridge` and `mw_to_ros2_bridge` executables
- Explicit String, Twist, and Image dispatch through `rclcpp::Serialization<T>`
- Canonical ROS type names plus stable versioned adapter wire identifiers
- SHM adapter transmit through `LoanedSample` and receive through `SampleView`
- UDS adapter compatibility through the owning middleware API
- ROS parameter configuration, launch file, and six-section YAML examples
- Bidirectional real-process ROS2 integration tests, including a 1280x720 RGB8 Image
- Real `ros2 topic pub --once`, normal cleanup, and bridge `SIGKILL` cleanup coverage

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
direct UDS mode. See [docs/FAILURE_MODEL.md](docs/FAILURE_MODEL.md) for liveness, crash, and cleanup
behavior, [docs/DATA_PLANE.md](docs/DATA_PLANE.md) for both payload paths,
[docs/CONTROL_PLANE.md](docs/CONTROL_PLANE.md) for discovery,
[docs/MEMORY_POOL.md](docs/MEMORY_POOL.md) for the pool layout, and
[docs/QUEUES_AND_LOANING.md](docs/QUEUES_AND_LOANING.md) for Phase 5 queue and RAII lifecycles.

## ROS2 Adapter

Build and install the core before building the independent ROS2 package:

```bash
cmake --install build --prefix "$PWD/_install"
source /opt/ros/$ROS_DISTRO/setup.bash
colcon --log-base log_ros2 build \
  --base-paths ros2_adapter \
  --build-base build_ros2 \
  --install-base install_ros2 \
  --cmake-args "-DCMAKE_PREFIX_PATH=$PWD/_install;/opt/ros/$ROS_DISTRO"
source install_ros2/setup.bash
```

Run a bridge with ROS parameters or use the installed launch file:

```bash
ros2 launch mw_ros2_adapter bridge.launch.py \
  direction:=ros2_to_mw_bridge \
  ros_topic:=/robot/text/in \
  mw_topic:=/robot/text \
  message_type:=std_msgs/msg/String \
  transport:=shm
```

See [docs/ROS2_ADAPTER.md](docs/ROS2_ADAPTER.md) for parameters, YAML examples, bidirectional
demos, type compatibility, shutdown behavior, and exact serialization/copy boundaries.

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
- Phase 7: ROS2 adapter (complete).
- Phase 8: Benchmarking, profiling, and evidence-based optimization.
- Phase 9: Final documentation and demos.

## Known Limitations

- Registry-discovered SHM supports N subscribers; the copied UDS baseline remains one-to-one.
- UDS notifications remain in use; `eventfd` and `SCM_RIGHTS` optimizations are deferred.
- The heartbeat lease covers process liveness while the registry daemon is running; it does not
  cover distributed hosts, kernel failure, power loss, or arbitrary memory corruption.
- The ROS2 adapter supports only String, Twist, and Image; it has no dynamic introspection or
  complete ROS schema-evolution system.
- The ROS2 serialized bridge path allocates/copies adapter buffers and is not end-to-end zero-copy.
- No benchmark framework, profiling result, ROS2 performance comparison, or final
  metrics/visualization system exists.
- Registry requests are synchronous and are not multiplexed across application threads.
- The UDS path copies payload data through kernel socket buffers and is not zero-copy.
- Ordinary SHM `publish()` copies into the pool, and owning `ReceivedMessage` copies from a
  `SampleView`; only the explicitly verified loan-to-view path avoids middleware payload copies.
- Recovery is limited to registry-known names and endpoint state; the daemon does not scan arbitrary
  `/dev/shm` or `/tmp` entries.
- A failed heartbeat control connection is not reconnected inside an existing `Context`; creating a
  new context creates a new session.
