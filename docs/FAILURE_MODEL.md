# 故障模型

## 范围

v1 Failure Model 覆盖单台 Linux 主机、一个正在运行的 `mw_registryd`、每个 Topic 一个 active
Publisher 和 N 个 Subscriber。系统通过 Control Socket EOF/HUP 或 missed Heartbeat lease
检测 Middleware 进程故障，并修复 Registry state 和该进程精确注册的 POSIX SHM/Socket 资源。
测试通过真实 `SIGKILL` 模拟 Publisher 和 Subscriber 故障。

系统不提供分布式共识、Persistence、Retransmission、exactly-once delivery、hard real-time
deadline，也不从主机/Kernel 故障或任意 Memory corruption 中恢复。

## Heartbeat 架构

每个启用 Registry 的 `Context` 共享一个 `RegistrySession`。注册返回单调分配的 `node_id`
和 `session_id`。随后 Session 打开一条专用 Control connection，attach 到该 identity，并启动
一个 RAII Heartbeat thread。Primary connection 继续处理同步注册/Discovery 调用，包括等待第一
个 Subscriber 的 Discovery Request。

Heartbeat 不携带 payload data。Response 包含当前 ALIVE state 和有界、去重的 Peer-death
metadata。Publisher/Subscriber 应用线程在 Data Plane operation 前消费这些 Event；Heartbeat
thread 从不修改 Queue、Pool、Connection vector 或 Chunk refcount。

Session 析构时会通知 Thread、唤醒 Condition Variable、join Thread、关闭 Heartbeat connection，
并通过 Primary connection 正常 unregister。

## Liveness State Machine

Registry 只使用 `std::chrono::steady_clock`：

```text
REGISTER or valid HEARTBEAT
          |
          v
        ALIVE -- suspect timeout --> SUSPECTED
          ^                            |
          | valid heartbeat            | dead timeout
          +----------------------------+----> DEAD -> record removed
```

对于该 Node/Session，DEAD 是 terminal state。之后的进程必须注册新 Session。SUSPECTED 状态下
收到 Heartbeat 会恢复为 ALIVE。`mwctl node list` 显示 ALIVE/SUSPECTED；DEAD Node 已被移除。

默认时间参数如下：

| Setting | Default |
| --- | ---: |
| heartbeat interval | 250 ms |
| suspect timeout | 750 ms |
| dead timeout | 1500 ms |

配置必须为正数，且满足 interval < suspect < dead。`mw_registryd` 接受
`--heartbeat-interval-ms`、`--suspect-timeout-ms` 和 `--dead-timeout-ms`。单元测试注入
精确 steady-clock time point，而不是 sleep。

## Control Connection 丢失

Primary Control connection 上的 EOF、HUP 或不可恢复错误会立即移除其 Node，并执行与 timeout
death 相同的幂等 Cleanup，不等待 Heartbeat deadline。仅丢失 Heartbeat connection 时会将其
detach；如果 Primary 仍打开但没有 Renewal，正常 SUSPECTED/DEAD timeout 继续生效。

## Session Identity

只有 Heartbeat connection 使用匹配的 Node ID 和唯一 Session ID 完成 attach 后，Heartbeat
才会被接受。活动 Record 中的 Node name 保持唯一。PID 可以出现在本地生成的 Resource name 中，
但不能作为 Session identity，因为操作系统会复用 PID。

## Registry Cleanup

移除 dead Node 前，`RegistryState` 会捕获一个有界 `DeadNodeCleanup` plan，其中包含：

- Publisher Pool descriptor；
- Subscriber Queue descriptor 和 Data Socket path；
- Publisher/Subscriber Endpoint ID 和定向 Peer-death event；
- Primary 和 Heartbeat connection identity。

随后 Server 移除精确 Node/Topic/Endpoint record，cancel 受影响的 pending Discovery，关闭配套
Control connection，执行资源 Cleanup，并为活动 Peer 排队 Event。它从不扫描 `/dev/shm` 或
`/tmp`。重复 Cleanup 可接受缺失 Record、closed descriptor、`ENOENT` 和已经 unlink 的 Name。

## Publisher 崩溃

对于 dead SHM Publisher，Registry unlink 精确 advertised Pool name，并向每个存活 Subscriber
Endpoint 发送带旧 Pool ID 的 `PublisherDead`。Subscriber 只丢弃属于该 Pool 的已排队 Handle，
关闭旧 Release channel，释放旧 Pool mapping，然后等待 replacement Publisher。如果新 Pool
Handle 在旧 Socket HUP 前刚刚到达，它会保留在 Queue，直到 replacement Wake 校验并安装对应
Pool descriptor。

Unlink Pool name 不会使已打开 mapping 或活动 `SampleView` 失效；这些本地 Object 会持有
Mapping 直到析构。LOANED Chunk 随 Publisher 一同消失，无需外部 Reference repair，因为整个
Pool 都会被移除。

## Subscriber 崩溃

对于 dead SHM Subscriber，Registry robust-open 精确注册的 Queue，必要时 repair mutex，将 Queue
标记为 Closed，broadcast blocked Producer，unlink Queue，并且仅当注册的 Data Socket path 仍是
Socket 时才移除。随后向活动 Publisher Endpoint 发送 `SubscriberDead`。其他 Subscriber 和
Queue 不变。

## Outstanding Reference Tracking

Publisher 是唯一的 Chunk refcount writer。每个已连接 Endpoint 都拥有一个尚未产生有效 Release
的 Published Handle vector。其预留并强制执行的上限是 Publisher Pool 中有限 Chunk 总数，因此
崩溃 bookkeeping 有界。

正常 Release 会校验 Pool ID、Chunk index、generation 和 offset，递减一个 Reference，并删除
一个匹配 obligation。Drop/timeout/failed-wake path 立即归还 tentative reference。Discovery
removal、Socket loss、dispatch 失败或 `SubscriberDead` 会在移除 Connection 前，恰好一次地
清理所有剩余 Endpoint obligation。Endpoint tombstone 防止重复 Cleanup，直到 Registry 不再
advertise 该 Endpoint。

这套机制可以修复 Subscriber 进程被杀死时由 `SampleView` 持有的 Chunk。该 Chunk 可回到 FREE，
并以新 generation 再次分配；针对旧 generation 的 delayed release 无法释放新 allocation。

## Robust Process-Shared Mutex

Queue Layout version 3 使用 `PTHREAD_PROCESS_SHARED` 和 `PTHREAD_MUTEX_ROBUST` 初始化
mutex。Condition wait 使用 `CLOCK_MONOTONIC`。

Lock 或 timed-wait acquisition 返回 `EOWNERDEAD` 时，Ring metadata 可能处于修改中间状态，
因此 Recovery 会把 Head、Tail 和 Size 重置为有效空状态，递增 owner-death metric，broadcast
Waiter，然后调用 `pthread_mutex_consistent`。Publisher outstanding tracking 修复被丢弃
Handle 的 Reference。`ENOTRECOVERABLE` 会作为明确的 synchronization error 返回。一个基于
fork 的测试会在进程持有并破坏 Mutex 时将其杀死，然后验证修复以及 Queue 可继续使用。

Registry 对 dead Subscriber 执行 Close 时也会 broadcast Condition。被
`BLOCK_WITH_TIMEOUT` 阻塞的 Publisher 会被唤醒，而不是等待完整配置 Timeout。

## SHM 所有权与 Cleanup

Publisher 拥有 Pool name、Mapping、free list、Chunk state 和 refcount。每个 Subscriber 拥有
Queue name、Queue mapping 和 Listener path。Peer 只拥有不带 Name ownership 的 Mapping。
Registry 保存精确 Descriptor，以便 Owner 死亡后 unlink 资源；不会假设相似名称的 Object 归属
该 Node。

正常退出依赖 Owner-led RAII cleanup 和显式 unregistration；崩溃退出依赖 Registry-led cleanup
与 application-thread Peer repair。两条路径最终产生相同的已移除 Registry state，并允许另一
条路径已经完成。

## 重连流程

活动 SHM Publisher 最多复用最后一次兼容 Discovery 结果 1 ms。空 Connection set、Socket/Queue
故障、Disconnect 或 Peer-death event 会立即使窗口失效；否则下一次有界刷新会移除已消失
Subscriber，连接新 Endpoint ID，并保留存活 Endpoint。活动 Subscriber 通过旧 Data Socket 或
Peer event 观察 Publisher death，只 reset 旧 Pool，然后接受 replacement connection。Registry
只有在 dead Publisher record 被移除后，才允许 replacement 通过单 Publisher 规则。

## 指标

`RegistryState` 记录 Heartbeat receive、ALIVE-to-SUSPECTED transition 和 dead-node cleanup
count。除 Queue Policy counter 外，Queue Stats 还记录 owner-death recovery 和 Peer reset
count。`mwctl stats` 暴露当前 Registry object snapshot 和三个累计 Liveness counter。
per-publication Queue、Drop、Block 和 Allocation metric 保存在 `PublishResult` 和 Benchmark
artifact 中；系统没有通用 Metrics export 或 visualization。

## 已知限制

- 现有 Context 不会在 Registry daemon 丢失后重连 Heartbeat/Control session。
- 即使某个应用线程仍在运行，停止 Heartbeat 的活动进程也会被视为 Dead；这是 Lease semantics。
- Queue owner-death repair 会有意重置不确定内容，可能丢弃 Sample。
- Registry Cleanup 只使用精确注册 Name；不会回收无关或预先存在的 stale resource。
- Event cache 有界，在极端 churn 下可能丢弃最旧 Event；Discovery 和 Socket reconciliation
  仍是当前 Endpoint 状态的备用权威来源。
- Robust pthread 行为和 Shared atomic layout 是本机 Linux/compiler ABI 假设。
- ROS2 Adapter 和自动 Benchmark 复用这些 Cleanup guarantee，但不会把 Failure Model 扩展到
  分布式主机、Registry restart recovery 或任意 Memory corruption。
