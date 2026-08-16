# 已知限制

以下内容是当前实现边界，不是已计划功能。

## 平台与范围

- 仅支持 Linux 本机 IPC，使用 Unix Domain Socket、POSIX Shared Memory、`epoll` 和 pthread
  process-shared synchronization。
- 仅支持单台主机，不包含远程传输或分布式发现。
- 每个 Topic 一个 active Publisher 和 N 个 Subscriber；未实现 Multi-Publisher ordering。
- Shared native atomic 和 robust pthread object 假设兼容的本机 Linux/compiler ABI。
- 仅使用普通 OS scheduling；不保证 hard real-time，也不设置 affinity、isolation 或 priority
  policy。

## Delivery 语义

- Volatile best-effort 行为受已配置 Queue Policy 约束；没有 Persistence、Durability、
  exactly-once delivery 或 Retransmission Protocol。
- `DROP_NEWEST` 和 `DROP_OLDEST` 会在压力下有意丢弃 Endpoint delivery。
- Robust mutex owner-death recovery 会重置不确定的 Queue content，可能丢弃 Sample。
- UDS 和 SHM Policy semantics 不完全等同于 ROS2 QoS。
- Registry 模式的 UDS Publisher 初次解析会连接当时发现的全部 Subscriber；只要仍有连接，
  晚加入的 Subscriber 不会自动加入现有扇出集合。
- 不提供 Security、Authentication、Authorization 或 Encryption。

## Copy 边界

- UDS 通过 Socket path 复制 payload，不是 zero-copy。
- SHM `Publisher::publish(data, size)` 把 application buffer 复制一次到 Pool Chunk。
- Owning `ReceivedMessage` 从 mapped memory 向外复制 byte。
- 只有原生 SHM `LoanedSample` 到 `SampleView` 路径经过验证，不产生 middleware payload
  copy。
- ROS2 Adapter 会 serialize/deserialize 并复制 Adapter buffer。它不是 end-to-end zero-copy，
  也不是 custom RMW。

## 故障恢复

- `mw_registryd` 自身丢失或重启后，现有 Context 不会自动重连。
- Recovery 覆盖已注册进程崩溃、精确注册资源和 Peer reconciliation；不修复主机/Kernel 故障
  或任意 Shared Memory corruption。
- Cleanup 从不扫描所有 `/dev/shm` 或 `/tmp` Name，无法回收无关或预先存在的 stale resource。
- 有界 Peer-event cache 在极端 churn 下可能丢弃最旧 Event；当前 Discovery 和 Socket state
  仍是备用信息来源。
- 不使用 Registry 的 Direct UDS mode 无法获得 Registry crash cleanup。

## API 与 Schema

- 类型兼容要求精确匹配 `type_name` 和 `type_hash`；没有 IDL compiler、dynamic
  introspection 或 Schema conversion。
- Registry Request 为同步调用，同一 Session 不支持并发 Call multiplexing。
- ROS2 Adapter 仅适配 ROS2 Jazzy 上的 `std_msgs/msg/String`、`geometry_msgs/msg/Twist` 和
  `sensor_msgs/msg/Image`；`sensor_msgs/msg/PointCloud2` 及其他 ROS2 消息类型不受支持。

## 测量

- 已提交 reference 在 WSL2、Intel i5-8300H 和普通后台 scheduling 环境中测量一次。它是该配置
  的证据，不代表普遍的 Transport ranking。
- Throughput profile latency 采用系统抽样，不是完整 tail distribution。
- direct ROS2 使用普通 `rmw_fastrtps_cpp`；结果包含 ROS serialization/DDS behavior。
- Profiling 时无法使用 `perf` 和 `strace`。分析依据 Benchmark delta、`/proc` counter、
  100 ms wait-channel sample 和源码检查，因此不声称 symbol-level CPU 或 dynamic syscall
  ranking。
- RSS 包含 Process/Library mapping 和有限的已配置 SHM mapping，不只包含活动 payload byte。

## 未实现

未实现 lock-free/SPSC replacement、`eventfd`、`SCM_RIGHTS`、`memfd_create`、per-thread
allocator cache、custom CPU scheduling、Multi-Publisher semantics、TCP、custom RMW、DDS/RTPS、
Persistence、Security 和分布式 Recovery。这些能力不在当前版本范围内。
