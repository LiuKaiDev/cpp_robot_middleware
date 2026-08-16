# 面试指南

## 项目叙事

本项目研究机器人 Pub/Sub API 之下实际发生的工作：从 copied UDS baseline 开始，将
Registry/Discovery 与 payload delivery 分离；加入预分配 SHM Pool 和 per-Subscriber Queue；
明确 Lifecycle 与 Backpressure；在进程故障后恢复已注册资源；通过 ROS2 Adapter 完成互通；
最后测量这些设计带来的权衡。

## 设计问题

### 为什么选择 Pub/Sub？

机器人 Producer 和 Consumer 应依赖带类型的 Topic contract，而非直接依赖进程身份。Registry
负责匹配 Endpoint，Data Plane 则保持进程间直连。

### 为什么需要 Registry 与 Discovery？

硬编码 Socket path 无法表达 Node、类型、启动顺序或 replacement。`mw_registryd` 分配身份，
强制一个 active Publisher，校验类型/transport 兼容性，并返回精确 Data Endpoint。

### Control Plane 与 Data Plane 有何区别？

注册、Discovery、Heartbeat、Stats 和 Cleanup 使用 Registry UDS Control Plane；UDS payload
frame 或 SHM Chunk/Queue 构成直接 Data Plane。Registry 从不复制 payload。

### 为什么每个 Topic 只允许一个 Publisher？

v1 专注于多进程 IPC、N-reader 生命周期和 Backpressure，避免引入 MPSC ordering、竞争性的
Pool ownership 或 Publisher arbitration。

### 为什么使用 Shared Memory？

大型 Socket payload 会为每个 Subscriber 跨越 Kernel boundary。SHM 将一个 Publisher-owned
payload 映射到 Subscriber 进程，并 fan-out 固定 Handle；代价是必须显式处理分配、同步和崩溃
恢复。

### 为什么 SHM 不自动等于 Zero-Copy？

普通 `publish(data, size)` 仍会把数据复制到 Chunk；owning receive 也会向外复制。只有经过
验证的原生 `LoanedSample` fill 到 `SampleView` read 路径避免 middleware payload copy。

### SHM Copy 与 SHM Loan 有何区别？

Copy 接受已有 application buffer 并执行一次 `memcpy`；Loan 允许应用直接写入 Pool Chunk。
优化后 reference 中，4 MiB p50 的 Copy 为 626.9 us，Loan 为 274.1 us；在被测主机上，
throughput 分别为 1500.0 和 1524.6 MiB/s。

### 为什么使用 Memory Pool 和 Size Class？

Hot path 复用固定 Chunk，而不是为每条消息映射或分配 payload storage。smallest-fitting class
对内存和 fragmentation 设定上限；耗尽时显式报错，不会回退到隐藏的无界分配。

### Chunk Lifecycle 与 Generation 有什么作用？

存储状态为 `FREE -> LOANED -> PUBLISHED -> RELEASED -> FREE`。每次分配都会改变 generation，
防止针对同一 index 的旧 release 影响新的 payload ownership。

### N 个 Subscriber 如何共享一个 Payload？

每个 Subscriber Queue 保存同一个逻辑 Handle，而不是复制后的字节。Publisher 为每个接受消息
的 Endpoint 持有一个 reference obligation，并在所有有效 release 或崩溃修复完成后回收 Chunk。

### 为什么使用 Ring Buffer？

固定容量 metadata storage 让内存使用量和满 Queue 行为清晰可见。v1 使用 robust
process-shared mutex 和 condition variable，因为正确性与恢复能力优先于未经 profiling 的
lock-free 重构。

### Backpressure 策略如何工作？

`DROP_NEWEST` 保留已排队工作，`DROP_OLDEST` 优先保留最新 sample，
`BLOCK_WITH_TIMEOUT` 等待空间但不允许 Producer 永久阻塞。策略按 Subscriber 独立配置。

## 故障问题

### 如何检测故障？

Primary Control EOF/HUP 会立即触发清理。独立 Heartbeat lease 覆盖 Socket 仍在但进程停止续租
的场景：ALIVE 先变为 SUSPECTED，随后变为 DEAD。

### 为什么使用 Robust Process-Shared Mutex？

如果进程在修改 Queue 时死亡，下一个加锁者会收到 `EOWNERDEAD`。此时 Queue index 被视为
不确定并重置；Publisher outstanding-handle tracking 负责修复被丢弃 entry 的引用。

### Subscriber 被 SIGKILL 后，未释放的 SampleView 怎么处理？

Publisher 的 per-Endpoint obligation 仍是权威记录。dead-Subscriber event 会恰好清理一次；
generation check 会拒绝之后到达的 stale release。

### Registry 死亡后系统能恢复吗？

现有 Context 不能自动恢复。Registry restart/reconnection 是明确记录的已知限制。

## 性能问题

### 为什么 UDS 在小消息下有竞争力？

固定的 SHM coordination 成本可能高于小型 Socket payload。优化后 reference 中，64-byte median
分别为 UDS 80.7 us、SHM Copy 170.0 us、SHM Loan 157.2 us；message rate 分别为 148.0k、
130.8k 和 151.4k/s。因此不存在普遍胜者。

### SHM 在大消息下有什么优势？

4 MiB 场景中，实测 p50 分别为 UDS 2053.4 us、Copy 626.9 us、Loan 274.1 us；delivered
throughput 分别为 1113.0、1500.0 和 1524.6 MiB/s。收益伴随着更大的有限 SHM mapping，以及
显式 Queue/Lifecycle 工作。

### p50 和 p99 是什么？

p50 是典型观测值的 median，p99 体现较慢的 tail。不能混用 fixed-rate latency case 和
throughput sampling；4 MiB 的样本数较少，也会让 tail 对比产生较大噪声。

### CPU 和 RSS 如何测量？

Runner 在确认过的 measurement boundary 采集 `/proc/<pid>/stat` CPU tick，并每 100 ms 采样
`/proc/<pid>/status` RSS。Publisher 与各 Subscriber 分开统计。

### Profiling 发现了什么？

小消息 SHM 曾在每次发布时同步执行 Registry resolve。Benchmark、context-switch 证据和源码
检查支持引入有界 1 ms 复用窗口。移除该成本后暴露出冗余 Wake 积累，随后通过 nonblocking
Wake drain 修复。由于 `perf` 和 `strace` 不可用，本文不声称 symbol 或 syscall 时间占比。

## ROS2 问题

### 为什么使用 Adapter 而不是 Custom RMW？

项目展示集成能力，但不承担 DDS Discovery、QoS mapping 和完整 RMW contract。独立 Adapter
使用普通 ROS2 API，并链接已安装的 Core。

### Adapter 与 RMW 有何区别？

Adapter 在应用层显式桥接选定的 ROS 类型和 Topic；RMW 是 ROS2 的通用 Middleware 实现边界。
本项目属于前者，且只支持 String、Twist 和 Image。

## 坦诚总结

最有力的证据不是“SHM 永远更快”，而是明确的 copy path、有界 ownership、fault test、完整且
非 cherry-pick 的矩阵、由证据支持的优化，以及明确记录的限制。详见
[已知限制](KNOWN_LIMITATIONS.md)。
