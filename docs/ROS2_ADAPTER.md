# ROS2 Adapter

## 架构与依赖方向

项目在不修改 Middleware Core 的前提下提供双向 Adapter：

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

Adapter 是 `ros2_adapter/mw_ros2_adapter` 中独立的 `ament_cmake` package。它调用
`find_package(mw CONFIG REQUIRED)`，并链接已安装的 `mw::mw_core` target。根 CMake build
不会进入 ROS package。Core、Registry、Protocol、Tool、Example 和 Core Test 源码都不包含或
链接 ROS2。

## 支持的消息类型

Adapter 明确支持：

| ROS type | 用途 |
| --- | --- |
| `std_msgs/msg/String` | 小型文本消息 |
| `geometry_msgs/msg/Twist` | 机器人速度指令 |
| `sensor_msgs/msg/Image` | 大型 Sensor payload |

未知值会使 Node 构造失败，输出 diagnostic 并以非零状态退出。系统没有 dynamic introspection、
plugin loader、IDL compiler 或 PointCloud2 support。

## Serialization 与类型兼容

`MessageCodec` 使用 Public `rclcpp::Serialization<T>` 和 `rclcpp::SerializedMessage` API，
不自行实现 CDR。Serialized ROS representation 是不透明的 Middleware payload。Deserialization
通过显式 input check 和 ROS2 serialization error 拒绝 empty、null、truncated 和 incompatible
payload。

Middleware 注册使用上述 canonical type 作为 `type_name`。`type_hash` 是对以下文本计算的
确定性 FNV-1a identifier：

```text
mw_ros2_adapter|ros2-cdr|v1|<canonical type name>
```

其打印形式以 `mw_ros2_adapter.cdr.v1.fnv1a64:` 开头。这是带版本的 Adapter wire
compatibility identifier，不具备 cryptographic 安全性，也不是完整 ROS IDL Schema hash 或
Schema-evolution system。两个 Bridge direction 使用同一函数，因此现有 Registry 继续强制精确
匹配 `type_name + type_hash + transport`。

## ROS2 到 Middleware 流程

Bridge executable 为 `ros2_to_mw_bridge`：

```text
typed ROS message
  -> ROS subscription callback on the executor thread
  -> rclcpp serialization into SerializedMessage
  -> adapter-owned serialized bytes
  -> SHM: Publisher::loan + one adapter memcpy + LoanedSample::publish
     UDS: Publisher::publish copied UDS path
  -> middleware subscriber
```

Adapter 不包含 worker thread。Middleware Publish failure 会连同对应 `ErrorCode` 一起记录，
包括 Pool exhaustion、Queue full/timeout、Connection loss 和 Registry/Type error。

## Middleware 到 ROS2 流程

Bridge executable 为 `mw_to_ros2_bridge`：

```text
middleware publisher
  -> SHM queue and SampleView, or owning UDS ReceivedMessage
  -> adapter copy into rclcpp::SerializedMessage storage
  -> rclcpp deserialization into the typed ROS message
  -> typed ROS publisher
```

一个短周期 ROS wall timer 执行 nonblocking Middleware take，每次 Callback 最多处理
`max_samples_per_poll` 条 Sample，不创建 Receive thread。在 ROS Publisher 至少发现一个
Subscriber 前，Bridge 会把 Sample 留在 Middleware Queue，避免 ROS Graph Discovery 对称前
执行 volatile ROS publish。

## Copy 与 Loan 边界

原生 Middleware `LoanedSample -> SampleView` SHM path 不产生 middleware payload copy。
ROS2 Adapter path 不同：

- ROS2 到 Middleware 会分配/使用 ROS serialized buffer，然后把该 Buffer 复制一次到 Middleware
  SHM Loan。
- Middleware 到 ROS2 直接从 `SampleView` 读取，但会先复制到
  `rclcpp::SerializedMessage` 拥有的 storage，再执行 ROS Deserialization。
- UDS 保留 Kernel/Socket copy 和 owning-message copy。

ROS2 Bridge 不是 end-to-end zero-copy，也不做性能声明。

## 参数

两个 executable 都接受 ROS parameter：

| Parameter | Default | 含义 |
| --- | --- | --- |
| `registry_socket` | `/tmp/mw_registry.sock` | 已存在的 `mw_registryd` Control Socket |
| `ros_topic` | `/mw_bridge/ros_data` | 带类型的 ROS Topic |
| `mw_topic` | `/mw_bridge/data` | Middleware Topic |
| `message_type` | `std_msgs/msg/String` | 一个受支持的 canonical ROS type |
| `transport` | `shm` | `shm` 或 `uds` |
| `max_message_size` | `4194304` | Serialized payload byte 上限 |
| `mw_node_name` | generated from ROS node/PID | Registry Node identity |
| `mw_socket_path` | generated under `/tmp` | Subscriber Data Socket path |
| `ros_qos_depth` | `10` | Reliable ROS KeepLast depth |
| `queue_depth` | `8` | Middleware SHM Subscriber Queue depth |
| `overflow_policy` | `drop_oldest` | `drop_newest`、`drop_oldest` 或 `block_with_timeout` |
| `block_timeout_ms` | `100` | Middleware Block Policy deadline |
| `poll_period_ms` | `2` | Reverse Bridge timer period |
| `max_samples_per_poll` | `16` | 有界 Reverse Bridge drain batch |

ROS Reliable KeepLast QoS 和 Middleware `OverflowPolicy` 位于不同层。二者不会自动等价，
Adapter 也不在二者之间进行转换。

## 构建与测试

先构建并安装 Core：

```bash
cmake -S . -B .work/public/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build .work/public/build_release -j
ctest --test-dir .work/public/build_release --output-on-failure
cmake --install .work/public/build_release --prefix "$PWD/.work/public/install"
```

然后构建独立 Adapter package：

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

测试使用隔离的 `ROS_DOMAIN_ID`、localhost Discovery、唯一 Node/Topic/Socket name、有界 Graph
polling 和 hard timeout。测试会启动真实 Registry、Bridge、ROS Publisher/Subscriber 和
Middleware Publisher/Subscriber 进程。

## Launch 与 YAML

已安装的 Launch file 可以启动任一 Direction：

```bash
ros2 launch mw_ros2_adapter bridge.launch.py \
  direction:=ros2_to_mw_bridge \
  ros_topic:=/robot/text/in \
  mw_topic:=/robot/text \
  message_type:=std_msgs/msg/String \
  registry_socket:=/tmp/mw_registry.sock \
  transport:=shm
```

`config/bridge_examples.yaml` 包含 String、Twist 和 Image 的两个 Direction。通过 remap Node
name 并传入文件选择一个 Section：

```bash
ros2 run mw_ros2_adapter mw_to_ros2_bridge --ros-args \
  -r __node:=mw_to_ros2_twist \
  --params-file .work/public/ros2/install/mw_ros2_adapter/share/mw_ros2_adapter/config/bridge_examples.yaml
```

不要把未隔离的 ROS Topic A 桥接到 Middleware Topic B，然后再反向桥接回同一个 ROS Topic A；
这会产生 feedback loop。Input 和 Output 应使用不同的 ROS Topic name。

## String Demo

在不同 ROS Topic 上启动 Registry、一个 Forward Bridge 和一个 Reverse Bridge：

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

使用相同 Topology、`geometry_msgs/msg/Twist` 和不同 Topic：

```bash
ros2 topic echo --once /public/twist/out geometry_msgs/msg/Twist
ros2 topic pub --once /public/twist/in geometry_msgs/msg/Twist \
  "{linear: {x: 1.25, y: -2.5, z: 0.0}, angular: {x: -0.125, y: 9.75, z: 3.141592653589793}}"
```

自动集成测试会双向校验全部六个 floating-point field。

## Image Demo

为 Bridge pair 配置 `sensor_msgs/msg/Image`、`/public/image/in`、`/public/image/out` 和
相同的 `/public/image` Middleware Topic。确定性集成测试是实际的大消息 Demo：

```bash
source /opt/ros/$ROS_DISTRO/setup.bash
ctest --test-dir .work/public/ros2/build/mw_ros2_adapter \
  -R mw_ros2_adapter_integration_test --output-on-failure
```

测试在两个 Direction 发送 1280x720 `rgb8` Image，并校验 Timestamp、Frame ID、Width、Height、
Encoding、Endian flag、Step、Data length 和每个 Byte。原始 Image data 为 2,764,800 bytes，
Jazzy serialized payload 为 2,764,860 bytes，低于现有 4 MiB 上限。

## Shutdown 与故障

SIGINT 会依次触发 `rclcpp` shutdown、Executor exit、Node destruction 和 Middleware
Endpoint/Context destruction。Core Heartbeat thread 通过 RAII join。正常 Cleanup 会移除
Registry record、Data Socket、Queue 和 Pool。Bridge 被 `SIGKILL` 时，Control connection
death cleanup 会移除精确注册资源；Adapter 不会自动重启。

Registry 不可用、不支持的 Type、无效 Parameter、Type mismatch、Serialization error、
Deserialization error 或 Middleware Publish error 都会产生 diagnostic。Registry daemon 重启
后，现有 Context 不会重连，这与已记录的 Failure boundary 一致。

## 已知限制

- 只支持 String、Twist 和 Image。
- Type identifier 是 Adapter-specific，不是完整 ROS Schema hash。
- ROS Serialization/Deserialization 会分配并复制 payload storage。
- DDS QoS 不会映射到 Middleware Queue Policy。
- 不包含 Loop detection、dynamic reconfiguration、Registry restart recovery、custom RMW、
  Benchmark、Profiling 或性能对比。

有界的端到端 String Bridge Demo 见 [Demo](DEMO.md)。
