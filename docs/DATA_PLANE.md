# Data Plane

## 范围

v1 Data Plane 提供两种可独立选择的 Linux 本机 payload transport，并包含 Copy/Loan 选择和
Peer crash recovery：

- `TransportType::UnixDomainSocket` 是 copied UDS baseline。
- `TransportType::SharedMemory` 是由 Registry 发现的预分配 POSIX SHM Pool transport。

两种模式下 Registry Control Plane 都使用 UDS。SHM mode 还保留 Publisher 到 Subscriber 的
direct UDS connection，用于固定 Wake/Release metadata；业务 payload byte 在 SHM mode 下
不会经过 UDS。

## UDS 基线

UDS path 发送显式编码的 24-byte `MW01` Header，随后发送恰好 `payload_size` 个字节。
Subscriber 递增接收 stream，并返回 owning `ReceivedMessage`。Direct mode 支持这条 baseline；
Registry mode 也可通过 `TransportType::UnixDomainSocket` 选择它。该路径选择一个已发现
Subscriber。

## SHM Pool 架构

```text
                         mw_registryd
                       UDS control plane
                         /          \
                    Publisher     Subscribers
                        |            ^  ^  ^
  SHM pool: metadata + reusable chunks |  |
                        +-- queues -----+--+
                        +-- UDS wakes --+--+
                        +<- RELEASEs ---+--+
```

Publisher 在 Endpoint 启动时创建并 map 一个 Pool。每个 Subscriber 创建一个有界 Ring Queue，
Publisher 在 Discovery 后 map 它。`publish()` 分配一个已有 Chunk，并把 application buffer
复制一次；`loan()` 则允许应用直接填充选中的 Chunk。Publish 会把同一个逻辑 Handle 加入每个
接受该消息的 Subscriber Queue。`waitAndTakeView()` 直接读取 mapped payload，
`waitAndTake()` 保留兼容的 owning copy。所有 Endpoint reference 被释放后，Publisher 回收
Chunk。Middleware 不包含 Data worker thread。

## SharedMemoryRegion 所有权

`SharedMemoryRegion` 是唯一的 mmap RAII wrapper。每个实例拥有自己的 fd 和 mapping，并在
析构时 close/unmap。Name ownership 单独管理：

- Publisher 为整个 Pool 执行一次 `shm_open(O_CREAT | O_EXCL | O_RDWR)`、`ftruncate` 和
  writable `mmap`；它拥有 POSIX name。
- Subscriber 对该 Publisher Pool 执行一次 `shm_open(O_RDONLY)` 和 read-only `mmap`。它会
  跨消息保留这个不拥有 name 的 mapping，并单独拥有一个 read-write Queue mapping。
- Publisher 以 read-write 打开每个已发现 Subscriber Queue，但不拥有其 SHM name。
- Publisher Region destructor 执行 best-effort 的正常 `shm_unlink`。

Unlink 会移除 name，但不会使已经打开的 mapping 失效。Publisher 死亡时，Registry unlink
精确 advertised Pool name；存活 Subscriber 在接受 replacement Publisher 前，只丢弃属于该
Pool 的 Handle。现有 mapping/view 在本地 Owner 释放前保持有效。Subscriber 死亡时，Registry
robust-close 并 unlink 其精确 Queue；活动 Publisher 释放该 Endpoint 的有界 outstanding Handle。

## 命名与 Segment Layout

Pool name 格式为 `/mw_p5_<publisher-pid>_<pool-id>`，Queue name 格式为
`/mw_q5_<subscriber-pid>_<queue-id>`。Name 不包含原始 Topic text，也不会随消息改变。

Segment 包含显式编码的 Pool Header、size-class metadata、Chunk directory，以及按 64-byte
对齐的 native Chunk Header 和固定容量 payload storage。Field layout、checked arithmetic、
atomic ABI 假设、State Machine 和校验规则详见 [Memory Pool](MEMORY_POOL.md)。

## Queue、Wake 与 Release

Queue entry 是一个固定的逻辑 Chunk Handle。Empty-to-nonempty Wake 是固定 272-byte UDS frame，
包含 Pool descriptor 和 Queue ID，不携带 Handle 或业务 payload。Release 固定为 32 bytes，
并重复 Handle identity。因为 Queue 而非 UDS stream 才是权威数据源，一个 Wake 可覆盖多个
entry。

Subscriber 安装 Publisher Pool mapping 后，`waitAndTakeView()` 会在检查 Shared Queue 前，
通过 nonblocking receive drain 多余的完整 Wake frame。这样可在 Queue 反复从 empty 变为
nonempty 时限制 metadata socket；Queue 仍是权威数据源，malformed、mismatched 或 disconnected
Wake stream 保持原有错误处理。

逻辑 Handle 包含 Pool ID、global Chunk index、allocation generation 和 payload offset。测试
在 Subscriber 进程间比较这些 Field，而不是比较无关的虚拟地址。

## Publish 与 Consume 顺序

Publisher 写入 payload 和 non-atomic metadata，初始化一个 guard reference，然后以 release-store
发布 `PUBLISHED`。每次 enqueue Queue 前先增加 tentative reference，所有 Endpoint decision
完成后才释放 guard。Subscriber 以 acquire-load 读取 state，校验重复 metadata，暴露 read-only
view，并在该 View 析构时发送 release。

Subscriber 不修改 free list，也不递减 reference。Publisher 是唯一的 refcount writer，并按
Endpoint 跟踪 outstanding Handle。Drop/timeout 会归还 tentative new reference；
`DROP_OLDEST` 会归还被替换的 reference。最后一次 release 将 Chunk 改为 `RELEASED`；
显式 reclaim 把它作为 `FREE` 放回对应 size-class free list。

Publisher 为每条 Connection 保留有界 outstanding-handle vector，上限为配置的有限 Pool Chunk
总数。有效 release 会删除一个匹配 obligation。Endpoint EOF/HUP、Registry dead-Subscriber
event、Discovery reconciliation 或 dispatch 失败都会在移除 Connection 前，恰好一次地清理
每个剩余 obligation。Generation check 防止复用的 Chunk index 被 stale release 影响。

## 错误处理

配置会拒绝未知 Transport、direct SHM mode、无效/未排序/为空的 size class、zero Queue depth/
count、无效 Policy/timeout 和 Layout overflow。Registry 会拒绝 Type/Transport mismatch 和无效
SHM Pool/Queue metadata。Subscriber 在访问前会拒绝错误 magic/version、不一致的
Pool/Topic/Queue identity、越界 metadata、misalignment、无效 Chunk index/offset、stale
generation、非 Published state 和越界 payload。

Pool/Queue 缺失、其他 `shm_open`/`ftruncate`/`mmap` 故障、Queue full/timeout/close、Pool
exhaustion、Socket disconnect 和显式 unlink failure 都有明确结果。Loan publish 失败会 consume
或 cancel Loan，不留下 LOANED Chunk。如果所有 Endpoint 都拒绝 Published Chunk，释放 Publisher
guard 即可回收它。

Data UDS 上的 `ECONNRESET` 和 `ENOTCONN` 被归类为 `ConnectionLost`，从而允许 Publisher
replacement。如果旧 Socket 报告 HUP 前，新 Pool Handle 已进入 Subscriber Queue，该 Handle
会留在 Queue；旧 Mapping 被 reset 后，replacement Wake 会先校验并安装新 Pool，再访问数据。

## Copy 语义与限制

普通 `publish()` 包含一次 application-buffer-to-SHM copy，owning `ReceivedMessage` 包含一次
mapped-SHM-to-vector copy。经过验证的 Loan path 在应用填充 `LoanedSample` 与通过
`SampleView` 读取同一个逻辑 Chunk 之间，不产生 middleware payload copy。这不代表 UDS、
所有 SHM API 或整个 Middleware 都是 zero-copy。性能测量及其限制见 [Benchmark](BENCHMARK.md)。

`eventfd` 和 `SCM_RIGHTS` 仍是未来候选项，不是已实现优化。Recovery 仅覆盖已注册资源，以及
通过 Control EOF/HUP 或 Heartbeat timeout 观测到的进程崩溃；不声称可以从任意 Shared Memory
corruption 或主机故障中恢复。Ownership 见[消息生命周期](MESSAGE_LIFECYCLE.md)，Profiling
测量及其限制见 [Benchmark](BENCHMARK.md)。
