# Shared Memory Pool 与 Chunk 生命周期

## 范围

每个 SHM Publisher 在 Endpoint 生命周期内拥有一个预分配 Pool。Subscriber Queue、Public
Loan/View API 和崩溃时的 Ownership repair 都围绕同一个 Chunk Lifecycle 工作。Enqueue/
Reference Protocol 和 Public RAII 生命周期见 [Queue 与 Loan](QUEUES_AND_LOANING.md)。

## Pool 生命周期与所有权

SHM Publisher 生成 `/mw_pool_<pid>_<pool-id>` Name，计算完整且经过检查的 Layout，通过 Registry
advertise descriptor，并使用现有 move-only `SharedMemoryRegion` 创建 POSIX object。创建过程
执行一次 `shm_open`、一次 `ftruncate` 和一次 writable `mmap`。Publisher 析构前，Pool
始终保持 mapping。

Publisher 拥有 SHM Name、writable mapping、free list、Chunk state change、reference-count
change、reclamation 和最终 `shm_unlink`。Subscriber 通过 Discovery/Wake metadata 获得 Name、
Pool ID、Segment size、Layout version 和 Topic ID。它在第一次 Wake 时创建一个 read-only、
不拥有 Name 的 mapping，并在 Endpoint 生命周期内保留。

Publisher 正常析构时会关闭 Data connection、unmap 并 unlink Pool；Subscriber 正常析构时
unmap 并 unsubscribe。Unlink 会移除 Name，但不会使已打开 mapping 失效。Publisher 死亡时，
Registry unlink 其精确 advertised Pool name，并通知存活 Subscriber。Daemon 不扫描
`/dev/shm`，也不根据 PID 推断 Ownership。

## 配置与 Size Class

`PublisherConfig::memory_pool` 是包含有序
`MemoryPoolClassConfig { chunk_size, chunk_count }` entry 的 C++ 配置。默认值为：

| Capacity | Count |
| ---: | ---: |
| 256 B | 32 |
| 4 KiB | 16 |
| 64 KiB | 8 |
| 1 MiB | 4 |
| 4 MiB | 2 |

Allocation 选择 Capacity 大于等于请求 payload 的最小 class，因此 1000 B 请求使用 4 KiB
Chunk。如果该 class 没有 Free Chunk，立即返回 `PoolExhausted`；不会阻塞、丢弃消息或借用
更大的 class。请求超过最大 class 时返回 `MessageTooLarge`。

## Segment Layout

单个 Segment 包含：

```text
+-------------------------------+
| encoded PoolHeader (80 B)     |
+-------------------------------+
| SizeClassMetadata[] (24 B)    |
+-------------------------------+
| ChunkDirectoryEntry[] (32 B)  |
+-------------------------------+ 64-byte aligned
| ChunkHeader 0 (64 B)          |
| Payload capacity 0            |
+-------------------------------+ 64-byte aligned
| ChunkHeader 1 + payload       |
| ...                           |
+-------------------------------+
```

显式编码的 Pool Header 包含 magic、Layout version、精确 Segment size、Pool ID、Topic ID、
Owner PID、数量和 metadata/storage offset。每条 size-class record 包含 Capacity、first global
Chunk index 和 Count。每条 Directory record 包含 Chunk Header offset、payload offset、
Capacity 和 class index。

Layout construction 使用 checked addition、multiplication 和 alignment。访问 payload byte 前，
Subscriber 会校验 magic、version、descriptor identity、精确 Segment size、数量、metadata
range、有序 size class、Directory range、64-byte alignment、non-overlap、Chunk index 和重复的
Chunk metadata。

## Chunk Header 与 Handle

每个按 64-byte 对齐的 `ChunkHeader` 包含 lock-free atomic `state` 和 `ref_count`、payload
size、Capacity、size-class index、generation、sequence、monotonic publish timestamp、Topic ID
和 Pool ID。Compile-time assertion 要求 `std::atomic<uint32_t>` 始终 lock-free。

固定的逻辑 Handle 为：

```text
pool_id
chunk_index
generation
payload_offset
```

不同进程中的 Mapping 可能具有不同虚拟地址，因此 equality 指四个逻辑 identity field 相同，
而不是 Pointer 相等。每次 allocation 都改变 generation，从而拒绝同一 index 上一次用途对应的
release。

Shared atomic 实现假设 Publisher 和 Subscriber 位于同一受支持 Linux 主机，并使用相同
compiler ABI 和 native 32-bit lock-free atomic representation。跨平台 Shared Memory ABI
兼容性不在 local-host v1 范围内。

## Lifecycle 与 Free List

正常 State Machine 为：

```text
FREE -> LOANED -> PUBLISHED -> RELEASED -> FREE
```

Allocation 从 Publisher-local per-class free list 移除一个 index，并执行
`FREE -> LOANED`。Copy path 写入一次 payload；Public `LoanedSample` path 只暴露该 Chunk 的
payload area，供应用直接填充。Publish 初始化 guard reference，并发布 State。最后一个有效
release 执行 `PUBLISHED -> RELEASED`；显式 reclaim 将 index 放回同一 class，并执行
`RELEASED -> FREE`。

只有 Publisher 操作 free list。Publisher-local `std::mutex` 串行化 allocation、Lifecycle
validation、Reference change 和 reclamation。预留的 vector capacity 表示 free-list pop/push
不会在 Hot path 分配 Chunk storage 或 payload。系统不包含 lock-free free list、per-thread cache
或 interprocess mutex。

无效 Transition、分配非 Free Chunk、在 `LOANED` 之外 Publish、Reference 非零时 reclaim、
duplicate release、无效 Index/Offset/Pool ID 和 stale generation 都会被拒绝。

## Multi-Subscriber 共享与 Release

Registry Control Protocol v5 返回所有兼容 Subscriber Endpoint 及其 Queue descriptor，同时返回
Publisher Pool descriptor。Publisher 为每个已发现 Subscriber 连接一条 Data UDS，并 map 一个
Queue。一次 Publish 执行：

```text
allocate one chunk
copy application payload into that chunk once
set ref_count = connected subscriber count
enqueue the same fixed ChunkHandle into N subscriber queues
process fixed RELEASE frames asynchronously on later calls/destruction
decrement exactly once per connection
reclaim at ref_count == 0
```

Subscriber 不直接递减 atomic。每个 `SampleView` 在析构时恰好发送一次 Release；owning API
通过临时 View 复制。Publisher 是唯一的 refcount writer，并在 vector 中按 Endpoint 跟踪
outstanding Handle，其上限为配置的有限 Chunk 数量。dead-Subscriber event、Socket disconnect、
dispatch 失败或 Discovery removal 会恰好一次地释放所有仍被跟踪的 obligation，包括随
Subscriber 进程被杀死而消失的 `SampleView` reference。

Copy API 在 Allocation 前完成初始 Discovery。Loan 可以在 Discovery 前分配，但 Publish 失败会
cancel Loan，或在 guard reference 之后 reclaim，因此不会仅仅因为没有 Endpoint 接受消息就让
Chunk 永久停留在 `LOANED` 或 `PUBLISHED`。

## Queue Metadata Protocol

Queue Wake 固定为 272 bytes，携带 Pool descriptor 和 Queue ID。Release 固定为 32 bytes，携带
逻辑 Chunk Handle。二者都使用显式 big-endian encoding，且不包含业务 payload。`ChunkHandle`
本身位于 Shared Ring Queue 中。

## Copy 与 Allocation 语义

普通 `publish()` 保留一次 application-buffer-to-SHM copy，owning `ReceivedMessage` 从临时
View 复制。原生 Loan-to-View path 在应用填充和 Subscriber 读取之间不产生 middleware payload
copy。所有 Path 都继续避免逐消息执行
`shm_open`/`ftruncate`/`mmap`/`munmap`/`shm_unlink`。

Chunk allocator 不执行 per-message payload allocation。Publisher Connection bookkeeping、
Wake/Release polling container、Control Protocol string/vector，以及每条 owning Subscriber
message 仍可分配普通进程 Heap memory。测量行为和 Profiling 限制见 [Benchmark](BENCHMARK.md)
及 `benchmark/profiling/`。

## 已知限制

- 没有 `eventfd` 或 `SCM_RIGHTS` 优化，继续使用 UDS Wake。
- 活动但无限期 stalled 的 Subscriber 只有在整个 Node 错过 Heartbeat dead timeout 后才会被
  reclaim；没有独立的 per-View lease。
- 现有 Context 不会从 Registry loss 中恢复，也不修复任意 Pool Header corruption。
- ROS2 Adapter 保留 serialization copy，且不会扩展原生 Loan/View 生命周期。
- Reference performance 依赖具体主机；测量结论以 [Benchmark](BENCHMARK.md) 为准，而不是
  Pool design。
