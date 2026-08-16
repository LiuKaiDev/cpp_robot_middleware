# Phase 7 Report

## Scope

Phase 7 adds an external, bidirectional ROS2 adapter while preserving the complete Phase 0-6
middleware core. The package provides typed bridges for String, Twist, and Image, consumes the
installed `mw::mw_core` package, supports SHM and UDS, and adds codec plus real-process integration
coverage. It does not add ROS dependencies to the core or implement a custom RMW.

## ROS Environment

- `ROS_DISTRO`: `jazzy`
- ROS prefix: `/opt/ros/jazzy`
- `rclcpp`: 28.1.21
- `std_msgs`, `geometry_msgs`, `sensor_msgs`: 5.3.8
- `ament_cmake`: 2.5.6
- `colcon`: `/usr/bin/colcon`
- Default RMW selected by the environment: `rmw_fastrtps_cpp`

The login shell did not initially expose `ros2`; every adapter command explicitly sourced
`/opt/ros/jazzy/setup.bash`. No package was installed and `/opt/ros` was not modified.

## Files Added

- `CODEX_TASKS/PHASE_7.md`
- `docs/ROS2_ADAPTER.md`
- `ros2_adapter/mw_ros2_adapter/package.xml`
- `ros2_adapter/mw_ros2_adapter/CMakeLists.txt`
- `ros2_adapter/mw_ros2_adapter/include/mw_ros2_adapter/bridge_config.hpp`
- `ros2_adapter/mw_ros2_adapter/include/mw_ros2_adapter/bridge_nodes.hpp`
- `ros2_adapter/mw_ros2_adapter/include/mw_ros2_adapter/message_codec.hpp`
- `ros2_adapter/mw_ros2_adapter/include/mw_ros2_adapter/type_support.hpp`
- `ros2_adapter/mw_ros2_adapter/src/bridge_config.cpp`
- `ros2_adapter/mw_ros2_adapter/src/bridge_nodes.cpp`
- `ros2_adapter/mw_ros2_adapter/src/ros2_to_mw_bridge.cpp`
- `ros2_adapter/mw_ros2_adapter/src/mw_to_ros2_bridge.cpp`
- `ros2_adapter/mw_ros2_adapter/src/type_support.cpp`
- `ros2_adapter/mw_ros2_adapter/launch/bridge.launch.py`
- `ros2_adapter/mw_ros2_adapter/config/bridge_examples.yaml`
- `ros2_adapter/mw_ros2_adapter/test/message_codec_test.cpp`
- `ros2_adapter/mw_ros2_adapter/test/bridge_integration_test.cpp`
- `ros2_adapter/mw_ros2_adapter/test/test_messages.hpp`
- `ros2_adapter/mw_ros2_adapter/test/test_peer.cpp`
- `PHASE_7_REPORT.md`

## Files Modified

- `.gitignore`: ignores colcon install/log output.
- `README.md`: updates project status, implemented features, adapter build/run instructions, and
  limitations.

No root CMake, middleware, registry, protocol, tool, core example, or core test source was changed.

## Architecture And Package Boundary

```text
ROS2 typed Pub/Sub
       |
       | rclcpp serialization
       v
mw_ros2_adapter (ament package)
       |
       | find_package(mw CONFIG REQUIRED), mw::mw_core
       v
installed libmw_core.so -> mw_registryd -> UDS / SHM
```

The adapter is not part of the root CMake graph. `mw_ros2_adapter_lib` holds shared bridge/config/
codec concerns; two small executable entry points provide `ros2_to_mw_bridge` and
`mw_to_ros2_bridge`. Colcon installs both executables, public adapter headers/library, launch, YAML,
and ament export metadata.

## Serialization And Type Compatibility

`MessageCodec` delegates to `rclcpp::Serialization<T>` and `rclcpp::SerializedMessage`; no CDR
implementation was added. Middleware payloads are opaque serialized ROS bytes.

The registry receives canonical ROS `type_name` values. `type_hash` is deterministic FNV-1a over
`mw_ros2_adapter|ros2-cdr|v1|<canonical type>`, printed with a versioned prefix. It is an adapter
wire identifier, not a cryptographic or complete ROS IDL schema hash. Existing exact registry type
and transport checks remain authoritative.

## Thread, Ownership, And Shutdown Model

ROS2-to-middleware publishes directly in the ROS subscription callback. Middleware-to-ROS2 uses a
wall timer, nonblocking take, and a bounded 16-sample default drain batch. There is no adapter data
thread, detached thread, or thread pool. The only background middleware thread is the existing
Phase 6 control heartbeat thread.

Each bridge node owns its middleware Context/endpoint and typed ROS entities. SIGINT causes ROS
shutdown and node/endpoint destruction. Normal RAII cleanup removes registry records, sockets,
queues, and pools. Phase 6 performs exact registry-led cleanup after `SIGKILL`.

## Data Flows And Copy Semantics

ROS2 to middleware:

```text
ROS message -> rclcpp serialization -> adapter bytes
            -> one memcpy into SHM LoanedSample -> publish
```

Middleware to ROS2:

```text
SHM SampleView -> one copy into rclcpp SerializedMessage
               -> ROS deserialization -> typed publish
```

UDS uses the owning middleware path in each direction and passed explicit String tests. The native
middleware loan-to-view path still avoids middleware payload copies. The serialized adapter path
allocates/copies and is not claimed end-to-end zero-copy.

## Supported Types And Codec Results

- String: PASS for empty, small, UTF-8, 256 KiB long value, and exact round trips.
- Twist: PASS for all six linear/angular fields with positive, negative, zero, and fractional values.
- Image: PASS for header timestamp/frame ID, width, height, encoding, step, endian flag, exact data
  size, and every data byte.
- Invalid/truncated payload: PASS; exception, no corrupted ROS publication.
- Wrong ROS payload: PASS; incompatible deserialization rejected.
- Unsupported PointCloud2 configuration: PASS; explicit diagnostic and exit status 2.
- Middleware type mismatch: PASS; registry rejected Twist publisher against Image bridge.

## Large Image Result

The process tests sent one deterministic 1280x720 RGB8 image in each direction:

| Field | Result |
| --- | ---: |
| Raw image bytes | 2,764,800 |
| Jazzy serialized payload | 2,764,860 |
| Middleware payload | 2,764,860 |
| Existing middleware limit | 4,194,304 |
| ROS2 -> middleware | PASS |
| Middleware -> ROS2 | PASS |
| Metadata and all bytes verified | PASS |

The existing 4 MiB pool class and message limit were not enlarged.

## Core Build And Regression

Commands:

```bash
cmake -S . -B build_core \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_core -j2
ctest --test-dir build_core --output-on-failure
```

Result: PASS. Clean GCC 13.3.0 C++17 build, 75/75 tests passed in 4.36 seconds. No pre-Phase 7 test
was removed, disabled, or renamed.

## Core Sanitizers

ASan commands used `-DENABLE_ASAN=ON` and
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`. Result: PASS, 75/75, no diagnostics.

UBSan commands used `-DENABLE_UBSAN=ON` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Result: PASS, 75/75, no diagnostics.

Adapter sanitizer execution was not claimed. ROS2 third-party libraries were exercised by the
normal colcon suite; the mandatory core sanitizer suites remained separate and reliable.

## Install, Export, And Core Independence

Commands:

```bash
cmake --install build_core --prefix "$PWD/_install"
cmake -S examples/external_consumer -B build_external_phase7 \
  -DCMAKE_PREFIX_PATH="$PWD/_install"
cmake --build build_external_phase7 -j2
./build_external_phase7/mw_external_consumer
ldd build_core/middleware/libmw_core.so
readelf -d build_core/middleware/libmw_core.so
```

Result: PASS. The external consumer printed `0.1.0`. `ldd`/`readelf` listed only `libstdc++`,
`libgcc_s`, and `libc` as direct dependencies; there was no rclcpp, RMW, rosidl, std_msgs,
geometry_msgs, or sensor_msgs link dependency. Source search found ROS includes/dependencies only
under `ros2_adapter/`; preexisting core test strings naming `sensor_msgs/Image` are compatibility
test data, not includes or links.

## ROS2 Adapter Build And Test

Commands:

```bash
source /opt/ros/jazzy/setup.bash
colcon --log-base log_ros2 build \
  --base-paths ros2_adapter \
  --build-base build_ros2 \
  --install-base install_ros2 \
  --cmake-args \
    -DCMAKE_BUILD_TYPE=Debug \
    '-DCMAKE_PREFIX_PATH=/home/chaos/projects/cpp_robot_middleware/_install;/opt/ros/jazzy'
colcon --log-base log_ros2 test \
  --base-paths ros2_adapter \
  --build-base build_ros2 \
  --install-base install_ros2
colcon --log-base log_ros2 test-result --test-result-base build_ros2 --verbose
```

Build result: PASS. Test result: PASS. Both CTest targets passed. The suite contains 8 codec/type
GTests and 14 real-process integration GTests, all passing; colcon reported 24 test result records,
0 errors, 0 failures, and 0 skipped.

## Bidirectional Integration Results

| Acceptance | SHM | UDS |
| --- | --- | --- |
| String ROS2 -> middleware | PASS | PASS |
| String middleware -> ROS2 | PASS | PASS |
| Twist ROS2 -> middleware | PASS | Not mandatory |
| Twist middleware -> ROS2 | PASS | Not mandatory |
| Image ROS2 -> middleware | PASS | Not mandatory |
| Image middleware -> ROS2 | PASS | Not mandatory |

Every mandatory case launched a real registry, bridge, custom middleware peer, and typed ROS peer.
Names were unique, discovery used bounded polling, waits had hard timeouts, and an isolated
`ROS_DOMAIN_ID` plus localhost discovery avoided the user's ROS graph.

## ROS2 CLI Acceptance

The automated process suite ran the equivalent of:

```bash
ros2 topic pub --once <unique-topic> std_msgs/msg/String \
  "{data: phase7-cli-payload}"
```

The CLI printed the exact String, returned status 0, and the custom middleware subscriber decoded
and verified `phase7-cli-payload`. Result: PASS.

Reverse acceptance was automated with typed rclcpp subscribers for String, Twist, and Image. This
is the deterministic automated result; `ros2 topic echo --once` remains the documented manual
smoke command.

## Launch And Config

`ros2 pkg executables mw_ros2_adapter` listed both bridges. `ros2 launch mw_ros2_adapter
bridge.launch.py --show-args` loaded the installed launch file and listed all arguments. The
installed YAML parsed successfully and contained six named sections: both directions for String,
Twist, and Image. Result: PASS.

## Registry, Normal Cleanup, And Crash Cleanup

Integration tests queried live `mwctl` state and required the bridge node and exact canonical topic
type to be present. Normal SIGINT then required the node, data socket, queue, and pool to disappear.
Result: PASS in every bidirectional scenario.

The crash smoke killed a live SHM `ros2_to_mw_bridge` with `SIGKILL`. The registry stayed alive,
removed the dead node, unlinked the exact publisher pool, and restored the project SHM namespace
after the remaining peer exited. No wildcard cleanup or adapter restart was used. Result: PASS.

Registry-unavailable startup also returned a clear connect diagnostic and status 2. Existing
contexts still do not reconnect after registry-daemon loss.

## Known Limitations

- Only String, Twist, and Image are supported.
- The adapter wire hash is deterministic/versioned but not a full ROS schema hash.
- ROS serialization/deserialization and adapter buffer transfer allocate/copy.
- ROS QoS and middleware overflow policy are separate and are not automatically mapped.
- There is no bridge-loop detection, dynamic reconfiguration, registry restart recovery, custom
  RMW, benchmark, profiling, or performance claim.
- The repository has not selected a license; the required ROS package license field remains
  `TODO` rather than inventing a project license during Phase 7.

## Phase Boundary

Phase 8 was not implemented.
