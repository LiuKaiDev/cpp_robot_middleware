# 简历指南

## 推荐标题

**Linux C++ High-Performance Shared-Memory Pub/Sub Middleware and ROS2 Adapter**

中文标题：**Linux C++ 高性能共享内存发布订阅中间件与 ROS2 适配框架**

## 简介

设计并实现一个 Linux 本机 C++17 多进程 Pub/Sub 中间件，包含版本化 UDS Control Plane、
UDS/SHM Data Plane、预分配 Memory Pool、多 Subscriber Chunk 共享、Backpressure、崩溃恢复、
ROS2 Adapter，以及可复现的跨进程 Benchmark。

## 中文简历要点

- 基于 Linux 与 C++17 自主设计本机多进程 Pub/Sub 中间件，将 UDS 注册发现控制面与
  UDS/共享内存数据面解耦，实现 Topic 类型校验、单 Publisher/N Subscriber 与运行状态查询。
- 设计预分配 Memory Pool、Generation 保护的 ChunkHandle、引用计数及每 Subscriber 有界队列，
  支持 DROP_NEWEST、DROP_OLDEST、BLOCK_WITH_TIMEOUT 和 LoanedSample/SampleView RAII 生命周期。
- 实现心跳租约、ALIVE/SUSPECTED/DEAD 状态机、robust process-shared mutex 与 Publisher/Subscriber
  SIGKILL 后的精确资源及未释放引用恢复；Core 83 项测试和 ROS2 Adapter 24 项测试全部通过。
- 构建 441 次自动化 Benchmark，对比 UDS、SHM Copy、SHM Loan 与 ROS2 Fast DDS；在 i5-8300H
  WSL2 Release 环境中，4 MiB 1-to-1 的 p50 为 2053.4/626.9/274.1 us，吞吐为
  1113.0/1500.0/1524.6 MiB/s，并依据 profiling 证据修复小消息逐次 discovery 开销。

## English Resume Bullets

- Designed a Linux/C++17 local multi-process Pub/Sub middleware with a versioned UDS control plane,
  registry-based discovery, and selectable UDS, SHM Copy, and SHM Loan data paths.
- Implemented preallocated size-class pools, generation-protected chunk handles, N-subscriber
  reference ownership, bounded ring queues, three backpressure policies, and move-only loan/view
  RAII APIs.
- Added heartbeat leases, ALIVE/SUSPECTED/DEAD detection, robust process-shared mutex recovery, and
  exact pool/queue/reference cleanup after real publisher/subscriber `SIGKILL` tests.
- Built a 441-run benchmark against direct ROS2 Fast DDS; on the recorded i5-8300H WSL2 Release
  environment, 4 MiB 1-to-1 p50 was 2053.4/626.9/274.1 us and throughput was
  1113.0/1500.0/1524.6 MiB/s for UDS/SHM Copy/SHM Loan.

## 关键技术

`C++17`、RAII、move semantics、atomic、pthread robust mutex/condition variable、Unix Domain
Socket、POSIX Shared Memory、`mmap`、`epoll`、CMake install/export、GoogleTest、Python
Benchmark automation、ROS2 Jazzy、`rclcpp`、Fast DDS。

## 量化证据背景

上述数字是 `benchmark/results/phase8_1_reference/` 中的 median：Release
`-O3 -DNDEBUG -g -fno-omit-frame-pointer`、WSL2、Intel i5-8300H、ROS2 Jazzy、
`rmw_fastrtps_cpp`、三次 repetition。它们不代表相对 ROS2 或 UDS 的普遍优势。

小消息的权衡：64 B 时，UDS p50 为 80.7 us，SHM Copy 为 170.0 us，SHM Loan 为 157.2 us；
最大 message rate 分别为 148.0k、130.8k 和 151.4k/s。这是一项有价值的面试证据，说明 Shared
Memory 不会自动胜出。

## 面试讨论要点

- Control Plane/Data Plane 分离为何能让 Discovery 离开 payload path。
- 为什么逻辑 Chunk 身份不能使用跨进程指针相等性。
- guard reference 如何避免 enqueue 与 reclaim 之间的竞争。
- 崩溃后为何同时需要 robust Queue repair 和 Publisher outstanding tracking。
- UDS、SHM Copy、owning receive 和 ROS2 Adapter 中仍有哪些复制。
- 为什么项目先选择正确、基于 mutex 的有界设计，再进行证据驱动的优化。
- 为什么 profiling 如实报告 `/proc` 证据，而不声称获得了不可用的 perf/strace 数据。

不要把本项目描述为生产级 Middleware、DDS replacement、custom RMW、分布式系统、lock-free
系统、完全 zero-copy 系统或硬实时系统。
