# Resume Guide

## Recommended Title

**Linux C++ High-Performance Shared-Memory Pub/Sub Middleware and ROS2 Adapter**

Chinese alternative: **Linux C++ 高性能共享内存发布订阅中间件与 ROS2 适配框架**

## Concise Summary

Designed and implemented a Linux-local C++17 multi-process Pub/Sub middleware with a versioned UDS
control plane, UDS/SHM data paths, preallocated memory pools, multi-subscriber chunk sharing,
backpressure, crash recovery, a ROS2 adapter, and a repeatable cross-process benchmark.

## Chinese Resume Bullets

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

## Key Technologies

`C++17`, RAII, move semantics, atomics, pthread robust mutex/condition variable, Unix Domain
Sockets, POSIX shared memory, `mmap`, `epoll`, CMake install/export, GoogleTest, Python benchmark
automation, ROS2 Jazzy, `rclcpp`, Fast DDS.

## Quantitative Evidence Context

The numbers above are medians from `benchmark/results/phase8_1_reference/`: Release
`-O3 -DNDEBUG -g -fno-omit-frame-pointer`, WSL2, Intel i5-8300H, ROS2 Jazzy,
`rmw_fastrtps_cpp`, three repetitions. They do not claim a universal advantage over ROS2 or UDS.

Small-message tradeoff: at 64 B, UDS p50 was 80.7 us versus 170.0 us SHM Copy and 157.2 us SHM
Loan; maximum message rates were 148.0k, 130.8k, and 151.4k/s. This is useful interview evidence
that shared memory does not automatically win.

## Interview Talking Points

- Why control/data separation keeps discovery off the payload path.
- Why logical chunk identity cannot use cross-process pointer equality.
- How the guard reference prevents enqueue-versus-reclaim races.
- Why robust queue repair and publisher outstanding tracking are both needed after a crash.
- Where copies remain in UDS, SHM Copy, owning receive, and the ROS2 adapter.
- Why the project chose a correct mutex-based bounded design before evidence-driven optimization.
- Why the profiling run reports `/proc` evidence honestly instead of claiming unavailable perf/strace data.

Do not describe this project as production middleware, a DDS replacement, a custom RMW,
distributed, lock-free, fully zero-copy, or hard real-time.
