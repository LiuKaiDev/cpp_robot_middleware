# 项目完成度清单

v1 必选范围已经完成。以下每项 PASS 都有当前源码、测试或已提交的测量证据支持，而非只依赖
历史开发记录。

| 必选要求 | 状态 | 证据 |
| --- | --- | --- |
| C++17 Middleware Core | PASS | [`middleware/CMakeLists.txt`](../middleware/CMakeLists.txt)、[架构](ARCHITECTURE.md) |
| Publisher API | PASS | [`publisher.hpp`](../middleware/include/mw/publisher.hpp) |
| Subscriber API | PASS | [`subscriber.hpp`](../middleware/include/mw/subscriber.hpp) |
| Unix Domain Socket 基线 | PASS | [UDS 基线](UDS_BASELINE.md)、进程/集成测试 |
| Registry daemon | PASS | [`registry/`](../registry/)、[Control Plane](CONTROL_PLANE.md) |
| Topic Discovery | PASS | [协议](PROTOCOL.md)、Registry Discovery 测试 |
| Node 注册 | PASS | [协议](PROTOCOL.md)、Registry 状态测试 |
| `mwctl` | PASS | node list、topic list/info、stats；Registry 集成测试 |
| Shared Memory transport | PASS | [Data Plane](DATA_PLANE.md)、SHM transport 测试 |
| Shared Memory RAII | PASS | `SharedMemoryRegion` 单元测试、[内存模型](MEMORY_MODEL.md) |
| Memory Pool | PASS | [Memory Pool](MEMORY_POOL.md)、Pool 单元测试 |
| Chunk 生命周期 | PASS | [消息生命周期](MESSAGE_LIFECYCLE.md)、Pool 测试 |
| Multi-Subscriber | PASS | 真实 1-to-4 SHM 和 UDS 集成测试 |
| 引用计数 | PASS | Pool 和 Multi-Subscriber 生命周期测试 |
| Subscriber Queue | PASS | Queue 单元测试和长时间 wraparound 测试 |
| Backpressure | PASS | 单元/集成测试和 Benchmark 证据覆盖三种策略 |
| Loaned Sample | PASS | move/cancel/publish/view 测试、[Queue 与 Loan](QUEUES_AND_LOANING.md) |
| 运行时指标 | PASS | `PublishResult`、Queue/Pool instrumentation、`mwctl stats`、Benchmark artifact |
| Heartbeat | PASS | 可注入 Liveness 单元测试和真实进程集成测试 |
| 崩溃检测 | PASS | Subscriber/Publisher `SIGKILL` 测试 |
| 资源清理 | PASS | 精确的 Pool/Queue/Socket/引用恢复测试 |
| ROS2 Adapter | PASS | [ROS2 Adapter](ROS2_ADAPTER.md)、String/Twist/Image 双向测试 |
| 自动 Benchmark | PASS | 441/441 次优化后运行、[Benchmark](BENCHMARK.md) |
| Direct ROS2 baseline | PASS | ROS2 Benchmark package 和优化后 reference |
| 单元测试 | PASS | Protocol、IPC、Pool、Queue、Lifecycle、Registry、Benchmark 单元测试 |
| 集成测试 | PASS | 跨进程、Discovery、SHM、Fanout、崩溃和 ROS2 测试 |
| 最终 README | PASS | [`README.md`](../README.md) |
| 架构文档 | PASS | [架构](ARCHITECTURE.md)和 Mermaid 图 |
| 可复现 Demo | PASS | [Demo 指南](DEMO.md)和 [`scripts/demo/`](../scripts/demo/) |

## 可选/后续事项

以下内容不属于 v1 必选项，且均为 **NOT IMPLEMENTED**：lock-free/SPSC Queue、`eventfd`、
`SCM_RIGHTS`、`memfd_create`、per-thread allocator cache、affinity/scheduler tuning、
PointCloud2、Multi-Publisher 语义、TCP/分布式 transport、custom RMW、DDS/RTPS、persistence、
security 和 Registry restart recovery。

精确的当前边界见[已知限制](KNOWN_LIMITATIONS.md)。
