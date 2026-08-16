# Unix Domain Socket Pub/Sub 基线

## 范围

Direct UDS mode 为一个 Publisher、一个 Subscriber 和一个显式配置的 Topic 提供 copied-payload
Linux 本机 baseline。它验证 Public API，并保留为 Registry/SHM path 的对照 Transport。它不是
Shared Memory 或 zero-copy transport。

## Connection Model

Subscriber 是 Unix Domain Socket Server：

```text
socket(AF_UNIX, SOCK_STREAM)
  -> bind(socket_path)
  -> listen()
  -> poll()/accept4()
```

Publisher 是 Client：

```text
socket(AF_UNIX, SOCK_STREAM)
  -> connect(socket_path)
  -> publish()
```

Socket path 通过 `PublisherConfig` 和 `SubscriberConfig` 提供，不硬编码在 Transport 内。
Direct mode 没有 Registry 或 Discovery service；由 Registry 发现的 UDS 见
[Control Plane](CONTROL_PLANE.md)。

## Frame Layout

每条消息编码为固定 24-byte Header，后接恰好 `payload_size` 个 byte：

| Offset | Size | Field | Encoding |
| --- | ---: | --- | --- |
| 0 | 4 | magic (`0x4D573031`, ASCII `MW01`) | big-endian |
| 4 | 4 | payload size | big-endian |
| 8 | 8 | publisher sequence | big-endian |
| 16 | 8 | publish timestamp in nanoseconds | big-endian |

实现显式编码每个 Field，不把 C++ struct layout 用作 wire format。分配 payload buffer 前，
Subscriber 会校验 magic 和配置的最大消息大小。允许 zero-byte payload，且仍携带完整 Header。

## Sequence 与 Timestamp

每个 Publisher instance 的 sequence 从 1 开始，只在完整 Frame 写入后递增。`publish()` 构造
Frame 时通过 `std::chrono::steady_clock` 获取 Timestamp。它是 monotonic nanosecond value，
不是 wall-clock time。

## Partial I/O

Publisher write 使用可安全处理 `EINTR` 的 `writeAll()` loop 和 `MSG_NOSIGNAL`。Subscriber
使用 nonblocking accepted Socket 和 `poll()`，并在 API call 之间保留 partial Header/Payload
state。因此 `take()` 可以在不丢弃未完成 Frame 的情况下返回，`waitAndTake()` 则对 Accept
和 Receive 工作使用同一个 Deadline。

## 资源所有权

- `UniqueFd` 是 move-only，并在析构时关闭 Descriptor。
- `Publisher` 拥有一个 connected Socket descriptor。
- `Subscriber` 拥有一个 listening descriptor；连接期间还拥有一个 accepted descriptor。
- `UnixListener` 在 bind 成功后拥有 Pathname。析构时先关闭 listening descriptor，再 unlink
  Pathname。
- 启动时不会盲目 unlink 现有 Pathname；活动或 stale Pathname 会使 bind 失败。在 Registry mode
  下，Node 死亡后 Daemon 会 unlink 精确注册的 Subscriber Socket；Direct mode 没有 Registry
  authority，继续保留原有的手动 stale-path 边界。

## Disconnect 与 Reconnect

没有 partial Frame 时发生 EOF，报告 `ConnectionLost`；partial Header 或 Payload 之后发生
EOF，报告 `InvalidFrame`。两种情况都只关闭 accepted Connection 并 reset Decoder，Listening
Socket 保持活动。之后的 Publisher instance 可连接同一个 Subscriber 并通信。Publisher 端 Peer
关闭会从 `publish()` 返回，不能通过 `SIGPIPE` 终止进程。

错误 magic、oversized frame 和 truncated frame 都是确定性的 Connection-level failure。在
Header 校验前不会根据不可信数据分配 payload。

## 线程与进程模型

Direct mode 没有 Middleware worker thread。启用 Registry 的 Context 拥有仅用于 Control 的
Heartbeat thread。`publish()` 在 Caller thread 上发送；`take()`/`waitAndTake()` 在 Caller
thread 上 poll、accept 和 receive。Publisher 和 Subscriber 应位于独立进程；测试也在同一进程
中覆盖这条 Transport。

## Demo

先启动 Subscriber：

```bash
./build/bin/mw_ping_subscriber \
  --socket /tmp/mw_uds_demo.sock \
  --count 100000 \
  --size 64
```

然后运行 Publisher：

```bash
./build/bin/mw_ping_publisher \
  --socket /tmp/mw_uds_demo.sock \
  --count 100000 \
  --size 64
```

两个程序都接受 `--timeout-ms`；该参数控制每次 Subscriber wait，Synchronous Publisher 会解析
但不使用它。

## 已知限制

- 只支持一个 Publisher、一个 Subscriber 和一个显式配置的 Topic。
- 没有 Registry、Discovery、Topic/Type negotiation 或 Multi-Publisher ordering。
- Payload 通过 Kernel Socket path 复制。
- 没有 Subscriber Queue、Backpressure Policy、Persistence、Retransmission 或 worker thread。
- Direct path 不包含 Shared Memory、Memory Pool、Loaned Sample 或自动 Crash cleanup。Registry
  mode Lifecycle recovery 另有文档。
- Direct mode Subscriber 非正常退出后留下的 Pathname 必须由 Operator 移除。
- 最终项目提供独立 ROS2 Adapter 和 Benchmark，但二者不会改变 Direct UDS mode 的所有权或
  Copy 语义。
