# Subscriber Queue 与 Loaned Shared Memory

## 范围

每个 SHM Subscriber 都有带明确 Overflow 行为的有界 Queue；Publisher 可暴露 writable Loan，
Subscriber 可保留 read-only View。Process-shared Queue mutex 是 robust mutex，并参与崩溃修复。
Queue 只保存 metadata，payload byte 保留在 Publisher-owned Memory Pool 中，详见
[Memory Pool](MEMORY_POOL.md)。

Copied UDS baseline 和普通 SHM `Publisher::publish()` path 均继续可用。

## Subscriber Queue 架构

每个 SHM `Subscriber` 在注册 Endpoint 前创建一个 POSIX SHM Queue。Queue name、ID、精确
Segment size、Capacity、Layout version、Overflow Policy 和 block timeout 会发送给 Registry。
Discovery 把 Descriptor 返回给 Publisher，后者打开 read-write mapping。Registry 保存该
Descriptor，只在需要 robust-close dead Subscriber 的已注册 Queue 时才 map 它。

```text
Publisher pool                         Subscriber endpoint
+--------------------+                 +---------------------------+
| ChunkHeader/payload| <--- handle --- | fixed-capacity ring queue |
+--------------------+                 +---------------------------+
         ^                                      ^
         | release                              | empty->nonempty wake
         +------------ data UDS ----------------+
```

Subscriber 拥有 Queue SHM name，并在正常析构时 unlink。Attachment count 使 process-shared
synchronization object 在最后一个正常 mapping detach 前保持有效。Publisher 只拥有自己的
mapping。Queue capacity 上限为 65,536 个 entry；depth zero 和 checked-size overflow 会被拒绝。

## Ring Buffer Layout

Native local-host Layout 由 `SubscriberQueueHeader`、alignment padding 和恰好 `capacity`
个 `ChunkHandle` slot 构成。Header state 包含 head、tail、当前 size、capacity、high-water
mark、close state、attachment count、Policy、timeout、Policy/repair counter、一个 robust
process-shared mutex 和一个 process-shared condition variable。Queue Layout version 3 新增
Benchmark 使用的精确 blocked-operation 和 blocked-time counter，不改变 Queue Policy。

每个 Entry 只包含：

```text
pool_id, chunk_index, generation, payload_offset
```

Ring 中不存放 payload byte、Pointer、vector、string 或带 Ownership 的 C++ object。Head/Tail
计算对 Capacity 取模。通过显式 size field 区分 Empty 和 Full。

## 同步模型

Queue mutation 使用 `PTHREAD_PROCESS_SHARED | PTHREAD_MUTEX_ROBUST` mutex。等待空间的
Producer 使用配置了 `CLOCK_MONOTONIC` 的 `PTHREAD_PROCESS_SHARED` condition variable。
Dequeue 唤醒一个 blocked Producer；Close broadcast 所有 Waiter。所有 Condition wait 都在
持有 mutex 时重新检查 Full/Closed predicate。

出现 `EOWNERDEAD` 时，获取 Lock 的进程将 Ring index/size 视为不确定状态，把 Queue 重置为空，
递增 `owner_death_recoveries`，broadcast Condition，然后调用 `pthread_mutex_consistent`。
这可能丢弃已排队 Handle；Publisher-side outstanding tracking 是修复其 reference 的权威来源。
`ENOTRECOVERABLE` 会成为明确的 synchronization error。系统不包含 lock-free Queue、Data Plane
worker thread，也不声称 hard real-time。Publisher/Subscriber 应用线程执行正常 Queue operation；
Registry event loop 只执行 dead-Subscriber close/repair。

## Queue 满时的语义

`SubscriberConfig` 提供 `queue_depth`、`overflow_policy` 和 `block_timeout`。Policy 按
Endpoint 生效，因此同一 Topic 上的 Subscriber 可选择不同 Capacity 和行为。

### DROP_NEWEST

Queue 满时，现有 Ring 不变，新 Handle 对该 Subscriber 被拒绝。Publisher 归还 tentative
reference；如果所有 Endpoint 都拒绝该 Sample，则报告 `QueueFull`。
`PublishResult::dropped_newest` 统计 Endpoint decision 数。

### DROP_OLDEST

Queue 满时，在 Queue mutex 下以 atomic 操作移除最旧 Handle 并插入新 Handle。随后 Publisher
移除并恰好一次地释放被替换的 Endpoint reference。如果这是最后一个 Reference，旧 Chunk 会被
reclaim。`PublishResult::dropped_oldest` 记录 replacement；因为最新 Sample 已被接受，Publish
仍成功。

### BLOCK_WITH_TIMEOUT

Queue 满时，Publisher 在 monotonic condition deadline 前等待，直到 Dequeue 创建空间、Queue
close 或 `block_timeout` 到期。到期只拒绝该 Endpoint reference；没有 Endpoint 接受 Sample
时报告 `QueueTimeout`。该 Timeout 表示 Queue Backpressure，而不是 dead-Subscriber detection。

## Reference 与 Enqueue Protocol

Publish 从一个 Publisher guard reference 开始。针对每个 Subscriber，Publisher 在把 Handle
暴露给 Queue 前增加 tentative reference；接受后该 Reference 转移给 Endpoint。Drop、timeout、
close、synchronization failure 或首次 Wake 失败都会立即归还 Reference。`DROP_OLDEST` 还会
归还被替换的 Endpoint reference。

所有 Endpoint decision 完成后，Publisher 才释放 guard。因此快速 Subscriber 无法在 Publisher
仍为后续 Subscriber 添加 Reference 时，将新 generation 降为零并允许复用。

Publisher 按 Connection 跟踪 outstanding Handle，并且是唯一修改 Pool refcount 或 free list
的进程。Vector 上限为配置的 Pool Chunk 总数。Subscriber release 是固定 metadata frame。
dead-Subscriber Cleanup 会恰好一次地清理该 Endpoint 的剩余 vector，并 tombstone 该 Endpoint，
直到 Discovery 将其移除。

## UDS Wake 与 Release

现有 Data UDS 继续充当 Notification channel。272-byte Wake 包含 Pool descriptor 和 Queue ID，
不包含 Handle 或业务 payload。32-byte Release 包含一个 `ChunkHandle`，不包含 payload。
只有 empty-to-nonempty transition 才发送 Wake。Subscriber 把 Ring 作为权威数据源，因此一个
Wake 可覆盖多个 Queue entry。

`eventfd` 优化已推迟，UDS 仍是支持的 Wake mechanism。

## LoanedSample 生命周期

`Publisher::loan(size)` 只适用于 SHM Publisher：

```text
FREE --loan--> LOANED --LoanedSample::publish--> PUBLISHED
  ^                |
  +---- cancel ----+  LoanedSample destructor without publish
```

`LoanedSample` 是 move-only object，恰好拥有一个 LOANED generation。`data()` 只暴露供应用
写入的 payload region；Pool Header 和 Lifecycle field 保持 private。`size()` 是请求的 payload
size，`capacity()` 是选中的 size-class capacity。未 Publish 即析构会 cancel 并归还 Chunk。
Move 会转移 Ownership 并使 Source inactive。第一次 `publish()` 无论成功或失败都会 consume
Object；第二次调用返回 `InvalidState`，不能再次 enqueue 或增加 Reference。

在 UDS 上，`loan()` 返回 `UnsupportedTransport`；不会分配一个假装是 Shared Memory Loan 的
Heap buffer。

## SampleView 生命周期

`Subscriber::takeView()` 和 `waitAndTakeView()` 暴露 SHM receive path。`SampleView` 是
move-only object，只提供 `const void* data() const`、size、sequence、monotonic publish
timestamp 和逻辑 Chunk identity；绝不暴露 writable shared payload memory。

一个 View 拥有一个 Subscriber reference。它保留 shared read-only Pool mapping 和 shared
release-channel context，而不是 raw `Subscriber*`。其 non-throwing destructor 恰好发送一次
Release。Moved-from View 不再生效。因此 View 可以安全地比 `Subscriber` object 存活更久，
并在自身析构前持续阻止 Chunk 被复用。

## Copy 与 Loan Path

SHM Copy Path 保持如下：

```text
Publisher::publish(application buffer)
  -> one middleware memcpy into pool chunk
  -> subscriber queue
  -> SampleView, or one copy into compatible ReceivedMessage
```

经验证的 Loan Path 为：

```text
application writes LoanedSample::data()
  -> publisher publishes that same logical pool/chunk/generation/offset
  -> subscriber reads it through SampleView
```

经过验证的 SHM Loan path 在 Publisher 填充 Loan 与 Subscriber `SampleView` 之间，不产生
middleware payload copy。该结论不适用于 UDS、普通 `publish()` 或兼容的 owning
`ReceivedMessage` API。不同进程不比较虚拟地址；相同 payload 由逻辑 Pool ID、Chunk index、
generation 和 payload offset 标识。

## 正常与崩溃 Cleanup

Subscriber 正常析构时关闭 Queue、唤醒 blocked Producer、drain 已排队 Handle 并发送 Release。
现有 `SampleView` 保留自己的 Release channel，之后再 Release。Publisher 正常析构时会有界
等待 outstanding View release，然后在 unmap/unlink Pool 前清理剩余已排队 Handle。

Subscriber 被 `SIGKILL` 后，Control EOF/HUP 或 Heartbeat timeout 会移除其 Endpoint。Registry
打开精确的已注册 Queue，必要时 robust-recover mutex，将 Queue 标记为 Closed，broadcast
blocked Producer 并 unlink Name。Peer-death event 通知 Publisher 释放所有仍 outstanding 的
Endpoint reference。其他 Subscriber 和 Queue 不受影响。重复 Cleanup 可接受缺失 Record 和已
unlink Name。

Publisher 死亡后，Registry unlink 精确 Pool name，并通知 Subscriber 只丢弃属于该 Pool 的
Entry，同时 reset 旧 Mapping/Connection。Replacement Publisher 随后可以连接并安装新的 Pool
descriptor。Recovery 不修复任意 Queue memory corruption，不在现有进程中恢复失败的 Registry
daemon，也不声称 hard real-time。独立 ROS2 Adapter 保留这些 Queue 语义，同时增加
serialization copy。
