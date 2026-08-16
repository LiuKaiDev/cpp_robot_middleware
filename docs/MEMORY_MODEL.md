# 内存模型

## Shared Memory 所有权

每个 SHM Publisher 在 Endpoint 生命周期内拥有一个预分配 POSIX Shared Memory Pool。它拥有
Name、writable mapping、free list、Chunk state、refcount 和正常 unlink。Subscriber 以 read-only
方式 map 该 Pool。每个 Subscriber 还单独拥有一个 read-write 有界 Queue；Publisher map 该
Queue，但不拥有其 Name。

`SharedMemoryRegion` 是管理 fd、mapping、unmap、close 和可选 Name ownership 的 move-only
RAII wrapper。每条消息都不会执行 `shm_open`、`ftruncate`、`mmap` 或 `munmap`。

## Pool Layout

```text
+--------------------------------+
| encoded PoolHeader (80 B)      |
+--------------------------------+
| SizeClassMetadata[] (24 B each)|
+--------------------------------+
| ChunkDirectoryEntry[] (32 B)   |
+--------------------------------+ 64-byte aligned
| ChunkHeader (64 B) + payload   |
| ...                            |
+--------------------------------+
```

编码后的 Header 记录 magic/version、精确 Segment size、Pool/Topic ID、Owner PID、数量和 Offset。
Directory entry 描述 Header/Payload offset、capacity 和 class。访问 payload 前会执行 checked
arithmetic，并校验 overlap/alignment。

## Size Class 与 Free List

默认 class 为 256 B x32、4 KiB x16、64 KiB x8、1 MiB x4 和 4 MiB x2。分配时选择能够容纳
payload 的最小 class。耗尽时返回 `PoolExhausted`，不会借用更大的 class 或分配无界 payload。

只有 Publisher 使用 per-class free-list vector。Publisher-local mutex 保护 allocation、
Lifecycle transition、refcount change 和 reclamation。实现有意不采用 lock-free allocator。

## Chunk Header

每个对齐的 native `ChunkHeader` 包含：

- atomic Lifecycle state 和 atomic `ref_count`；
- payload size 和 capacity；
- size-class index 和 allocation generation；
- sequence 和 monotonic publish timestamp；
- Topic ID 和 Pool ID。

受支持的 Linux/compiler 配置要求 native 32-bit atomic 始终 lock-free。该 Shared native Header
是 local-host ABI，不是 portable file format 或 network format。

## 逻辑 Chunk 身份

`ChunkHandle` 只包含值：

```text
(pool_id, chunk_index, generation, payload_offset)
```

不同进程可能把同一 Object map 到不同虚拟地址，因此不能跨进程比较 Pointer。每次 allocation
都会递增 generation，避免旧的 delayed release 影响同一 index 的新用途。

## 引用所有权

Publisher 是唯一的 refcount writer。Publish 从一个 guard reference 开始，在每次 Queue enqueue
前增加一个 tentative reference，并在所有 Endpoint decision 结束后释放 guard。成功进入 Queue
的 entry 会把 tentative reference 转移给对应 Endpoint。Drop、timeout、failed wake 或被
`DROP_OLDEST` 替换的 Handle 会归还相应 reference。

每条 Publisher Connection 跟踪一个有界 outstanding Handle vector。`SampleView` destructor
发送一次 Release frame。Disconnect 或 dead-Subscriber reconciliation 会恰好一次地清理所有
剩余 obligation。

## SHM Copy Path

```text
application buffer
  -> one memcpy into an allocated pool chunk
  -> N queues receive the same handle
  -> SampleView reads mapped bytes
```

调用 owning Subscriber API 会增加一次 mapped-memory-to-vector copy。

## SHM Loan Path

```text
Publisher::loan(size)
  -> application fills LoanedSample::data() in the pool
  -> publish the same logical chunk
  -> SampleView reads that chunk
```

经验证，这条原生 Loan-to-View path 不产生 middleware payload copy。ROS2 Adapter 仍会先
serialize 到 Adapter buffer，再把 serialized byte 复制进 Loan。

## Cleanup

正常 Owner 通过 RAII 执行 close/unmap/unlink。Publisher 死亡时，Registry unlink 精确 Pool
name；存活 Subscriber 在现有本地 View 结束后，仅 reset 该 Pool。Subscriber 死亡时，Registry
robust-close/unlink 其精确 Queue，活动 Publisher 修复 outstanding reference。任意 Memory
corruption 和 Registry daemon restart 不在 recovery model 内。

Field-level validation 见 [Memory Pool](MEMORY_POOL.md)，Transition 见
[消息生命周期](MESSAGE_LIFECYCLE.md)。
