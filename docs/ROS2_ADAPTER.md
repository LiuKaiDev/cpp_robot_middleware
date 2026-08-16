# ROS2 Adapter

## Architecture And Dependency Direction

The project provides a bidirectional adapter without changing the middleware core:

```text
ROS2 applications
      |
      | rclcpp typed Pub/Sub and serialization
      v
mw_ros2_adapter
      |
      | installed public API: mw::mw_core
      v
Middleware core -> mw_registryd -> UDS or SHM data plane
```

The adapter is an independent `ament_cmake` package in
`ros2_adapter/mw_ros2_adapter`. It calls `find_package(mw CONFIG REQUIRED)` and links the installed
`mw::mw_core` target. The root CMake build does not enter the ROS package. No core, registry,
protocol, tool, example, or core test source includes or links ROS2.

## Supported Message Types

The adapter explicitly supports:

| ROS type | Intended use |
| --- | --- |
| `std_msgs/msg/String` | Small text messages |
| `geometry_msgs/msg/Twist` | Robot velocity commands |
| `sensor_msgs/msg/Image` | Large sensor payloads |

Unknown values fail node construction with a diagnostic and nonzero process exit. There is no
dynamic introspection, plugin loader, IDL compiler, or PointCloud2 support.

## Serialization And Type Compatibility

`MessageCodec` uses the public `rclcpp::Serialization<T>` and `rclcpp::SerializedMessage` APIs. It
does not implement CDR itself. The serialized ROS representation is the opaque middleware payload.
Deserialization rejects empty, null, truncated, and incompatible payloads through explicit input
checks and ROS2 serialization errors.

Middleware registration uses the canonical type above as `type_name`. `type_hash` is a deterministic
FNV-1a identifier over:

```text
mw_ros2_adapter|ros2-cdr|v1|<canonical type name>
```

Its printed form starts with `mw_ros2_adapter.cdr.v1.fnv1a64:`. This is a versioned adapter wire
compatibility identifier. It is not cryptographic and is not a complete ROS IDL schema hash or a
schema-evolution system. Both bridge directions use the same function, so the existing registry
continues to enforce exact `type_name + type_hash + transport` compatibility.

## ROS2 To Middleware Flow

The bridge executable is `ros2_to_mw_bridge`:

```text
typed ROS message
  -> ROS subscription callback on the executor thread
  -> rclcpp serialization into SerializedMessage
  -> adapter-owned serialized bytes
  -> SHM: Publisher::loan + one adapter memcpy + LoanedSample::publish
     UDS: Publisher::publish copied UDS path
  -> middleware subscriber
```

There is no adapter worker thread. A middleware publish failure is logged with its `ErrorCode`,
including pool exhaustion, queue full/timeout, connection loss, and registry/type errors.

## Middleware To ROS2 Flow

The bridge executable is `mw_to_ros2_bridge`:

```text
middleware publisher
  -> SHM queue and SampleView, or owning UDS ReceivedMessage
  -> adapter copy into rclcpp::SerializedMessage storage
  -> rclcpp deserialization into the typed ROS message
  -> typed ROS publisher
```

A short ROS wall timer performs nonblocking middleware takes and handles at most
`max_samples_per_poll` samples per callback. It does not create a receive thread. The bridge leaves
samples in the middleware queue until its ROS publisher sees at least one subscriber, avoiding a
volatile ROS publication before graph discovery is symmetric.

## Copy And Loan Boundaries

The native middleware `LoanedSample -> SampleView` SHM path avoids
middleware payload copies. The ROS2 adapter path is different:

- ROS2 to middleware allocates/uses a ROS serialized buffer, then copies that buffer once into the
  middleware SHM loan.
- Middleware to ROS2 reads directly from `SampleView`, but copies into storage owned by
  `rclcpp::SerializedMessage` before ROS deserialization.
- UDS keeps its kernel/socket and owning-message copies.

The ROS2 bridge is not end-to-end zero-copy and makes no performance claim.

## Parameters

Both executables accept ROS parameters:

| Parameter | Default | Meaning |
| --- | --- | --- |
| `registry_socket` | `/tmp/mw_registry.sock` | Existing `mw_registryd` control socket |
| `ros_topic` | `/mw_bridge/ros_data` | Typed ROS topic |
| `mw_topic` | `/mw_bridge/data` | Middleware topic |
| `message_type` | `std_msgs/msg/String` | One supported canonical ROS type |
| `transport` | `shm` | `shm` or `uds` |
| `max_message_size` | `4194304` | Maximum serialized payload bytes |
| `mw_node_name` | generated from ROS node/PID | Registry node identity |
| `mw_socket_path` | generated under `/tmp` | Subscriber data socket path |
| `ros_qos_depth` | `10` | Reliable ROS KeepLast depth |
| `queue_depth` | `8` | Middleware SHM subscriber queue depth |
| `overflow_policy` | `drop_oldest` | `drop_newest`, `drop_oldest`, or `block_with_timeout` |
| `block_timeout_ms` | `100` | Middleware block-policy deadline |
| `poll_period_ms` | `2` | Reverse bridge timer period |
| `max_samples_per_poll` | `16` | Bounded reverse-bridge drain batch |

ROS reliable KeepLast QoS and middleware `OverflowPolicy` operate at different layers. They are not
automatically equivalent and the adapter does not translate one into the other.

## Build And Test

Build and install the core first:

```bash
cmake -S . -B .work/public/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build .work/public/build_release -j
ctest --test-dir .work/public/build_release --output-on-failure
cmake --install .work/public/build_release --prefix "$PWD/.work/public/install"
```

Then build the independent adapter package:

```bash
source /opt/ros/$ROS_DISTRO/setup.bash
colcon --log-base .work/public/ros2/log build \
  --base-paths ros2_adapter \
  --build-base .work/public/ros2/build \
  --install-base .work/public/ros2/install \
  --cmake-args \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_PREFIX_PATH=$PWD/.work/public/install;/opt/ros/$ROS_DISTRO"
colcon --log-base .work/public/ros2/log test \
  --base-paths ros2_adapter \
  --build-base .work/public/ros2/build \
  --install-base .work/public/ros2/install
colcon --log-base .work/public/ros2/log test-result \
  --test-result-base .work/public/ros2/build --verbose
source .work/public/ros2/install/setup.bash
```

Tests use an isolated `ROS_DOMAIN_ID`, localhost discovery, unique node/topic/socket names, bounded
graph polling, and hard timeouts. They launch real registry, bridge, ROS publisher/subscriber, and
middleware publisher/subscriber processes.

## Launch And YAML

The installed launch file starts either direction:

```bash
ros2 launch mw_ros2_adapter bridge.launch.py \
  direction:=ros2_to_mw_bridge \
  ros_topic:=/robot/text/in \
  mw_topic:=/robot/text \
  message_type:=std_msgs/msg/String \
  registry_socket:=/tmp/mw_registry.sock \
  transport:=shm
```

`config/bridge_examples.yaml` contains both directions for String, Twist, and Image. Select a
section by remapping the node name and passing the file:

```bash
ros2 run mw_ros2_adapter mw_to_ros2_bridge --ros-args \
  -r __node:=mw_to_ros2_twist \
  --params-file .work/public/ros2/install/mw_ros2_adapter/share/mw_ros2_adapter/config/bridge_examples.yaml
```

Do not connect an unisolated ROS topic A to middleware topic B in both directions back to the same
ROS topic A. That creates a feedback loop. Use distinct input and output ROS topic names.

## String Demo

Start the registry, one forward bridge, and one reverse bridge on separate ROS topics:

```bash
./_install/bin/mw_registryd --socket /tmp/mw_registry.sock

ros2 run mw_ros2_adapter ros2_to_mw_bridge --ros-args \
  -p ros_topic:=/public/string/in -p mw_topic:=/public/string \
  -p message_type:=std_msgs/msg/String -p transport:=shm

ros2 run mw_ros2_adapter mw_to_ros2_bridge --ros-args \
  -p ros_topic:=/public/string/out -p mw_topic:=/public/string \
  -p message_type:=std_msgs/msg/String -p transport:=shm

ros2 topic echo --once /public/string/out std_msgs/msg/String
ros2 topic pub --once /public/string/in std_msgs/msg/String "{data: public-demo}"
```

## Twist Demo

Use the same topology with `geometry_msgs/msg/Twist` and distinct topics:

```bash
ros2 topic echo --once /public/twist/out geometry_msgs/msg/Twist
ros2 topic pub --once /public/twist/in geometry_msgs/msg/Twist \
  "{linear: {x: 1.25, y: -2.5, z: 0.0}, angular: {x: -0.125, y: 9.75, z: 3.141592653589793}}"
```

The automated integration suite validates all six floating-point fields in both directions.

## Image Demo

Configure the bridge pair with `sensor_msgs/msg/Image`, `/public/image/in`,
`/public/image/out`, and the same `/public/image` middleware topic. The deterministic integration
test is the practical large-message demo:

```bash
source /opt/ros/$ROS_DISTRO/setup.bash
ctest --test-dir .work/public/ros2/build/mw_ros2_adapter \
  -R mw_ros2_adapter_integration_test --output-on-failure
```

It sends a 1280x720 `rgb8` Image in both directions and checks timestamp, frame ID, width, height,
encoding, endian flag, step, data length, and every byte. Raw image data is 2,764,800 bytes and its
Jazzy serialized payload is 2,764,860 bytes, below the existing 4 MiB limit.

## Shutdown And Failures

SIGINT causes `rclcpp` shutdown, executor exit, node destruction, and middleware endpoint/context
destruction. The core heartbeat thread is RAII-joined. Normal cleanup removes registry records,
data sockets, queues, and pools. If a bridge is killed with `SIGKILL`, control-connection death
cleanup removes its exact registered resources; the adapter does not auto-restart.

An unavailable registry, unsupported type, invalid parameter, type mismatch, serialization error,
deserialization error, or middleware publish error produces a diagnostic. Existing contexts do not
reconnect after a registry daemon restart, matching the documented failure boundary.

## Known Limitations

- Only String, Twist, and Image are supported.
- The type identifier is adapter-specific, not a complete ROS schema hash.
- ROS serialization/deserialization allocates and copies payload storage.
- DDS QoS is not mapped to middleware queue policy.
- No loop detection, dynamic reconfiguration, registry restart recovery, custom RMW, benchmark,
  profiling, or performance comparison is included.

The bounded end-to-end String bridge demo is documented in [DEMO.md](DEMO.md).
