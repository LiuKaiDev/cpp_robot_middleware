# Registry Control Plane

## 范围与分离

最终 v1 Control Plane 在单台 Linux 主机上提供基于 Registry 的 Discovery、Transport/Pool/Queue
metadata、Liveness session、只读检查和精确的崩溃清理，同时保持 Control 与 payload 职责分离：

```text
Context / Publisher / Subscriber / mwctl
                 |
                 | Control Protocol
                 v
       mw_registryd control UDS

Publisher --------------------------> Subscriber
       UDS payload frame, or queue wake/release UDS
```

`mw_registryd` 保存 identity 和 Endpoint metadata，不代理也不复制用户 payload。默认 Control
path 为 `/tmp/mw_registry.sock`；可通过 `RegistryConfig`、`mw_registryd --socket` 或
`mwctl --registry` 选择其他路径。

## Registry 架构

- `ControlProtocol` 显式编码 Header、带类型的 payload field 和 Response envelope。
- `RegistryState` 拥有 Node、Topic、Publisher 和 Subscriber map，并执行匹配规则，不包含
  Socket I/O。
- `RegistryServer` 拥有 listening socket、已接受的 Control connection、`epoll` descriptor、
  partial input/output buffer、pending Discovery request 和 `RegistryState`。
- `RegistryClient` 为 Middleware Context 和 `mwctl` 发送同步、可关联的 Request。
- 启用 Registry 的 `Context` 拥有一个共享 `RegistrySession`；Endpoint 会保留该 Session
  直到自身析构，因此原始 Context 消失后仍可清理 Endpoint。
- 每个 Session 拥有第二条 Control connection 和一个 RAII Heartbeat thread。Primary connection
  继续处理同步调用，Heartbeat connection 定期续租 Node lease 并接收 Peer-death event。

Daemon 使用一个 event-loop thread。应用拥有一个仅用于 Control 的 Heartbeat thread；所有
Data Plane、Discovery、publish、receive、Queue repair 和 reference repair 都在 Caller thread
上执行。

## Control Socket 与 Header

Control Plane 使用 `AF_UNIX`、`SOCK_STREAM`。每个 frame 以如下逻辑 16-byte Header 开头：

| Offset | Size | Field | Encoding |
| --- | ---: | --- | --- |
| 0 | 4 | magic (`0x4D574332`, `MWC2`) | big-endian |
| 4 | 2 | protocol version (`5`) | big-endian |
| 6 | 2 | opcode | big-endian |
| 8 | 4 | request ID | big-endian |
| 12 | 4 | payload size | big-endian |

Version 5 保留 Pool 与 Subscriber Queue descriptor，并加入 Node session ID、Heartbeat attach/
renewal、Liveness state 和有界 Peer-death event。Resolve 返回所有兼容 Subscriber 的 Endpoint
ID、Socket path、Size bound 和 Queue descriptor 的计数列表。Header 按 Field 逐一编码，绝不把
C++ struct 作为 wire ABI 发送。

在分配前，Payload 被限制为 64 KiB。Server 会保留不完整 stream input，直到完整 Header 和声明
的 payload 均已到达。错误 magic、不支持的 version、oversized payload、未知 opcode、malformed
payload 和 truncated connection 都不能触发部分状态变更。

## Opcode 与 Response

当前 Protocol 定义以下 Operation：

| Opcode | 职责 |
| --- | --- |
| `REGISTER_NODE` / `UNREGISTER_NODE` | 创建或正常移除 Node identity 及其 Endpoint |
| `ADVERTISE_TOPIC` / `UNADVERTISE_TOPIC` | 创建或移除唯一的 active Publisher |
| `SUBSCRIBE_TOPIC` / `UNSUBSCRIBE_TOPIC` | 创建或移除 Subscriber Data Endpoint |
| `RESOLVE_ENDPOINT` | 返回兼容的 Subscriber Socket、Queue descriptor 和 SHM Pool metadata |
| `LIST_NODES` | 返回已排序的活动 Registry Node record |
| `LIST_TOPICS` | 返回已排序的活动 Topic record |
| `QUERY_TOPIC` | 返回类型、Size、Publisher 数和 Subscriber 数 |
| `QUERY_STATS` | 返回当前 object 数量和累计 Liveness counter |
| `ATTACH_HEARTBEAT` | 将独立 Connection 绑定到一个 Node/Session identity |
| `HEARTBEAT` | 续租 Lease，并返回 Liveness 和有界 Peer-death event |
| `RESPONSE` | 携带 Error code、message 和 operation-specific body |

每个 Request 的 Response 都带回相同 `request_id`。Response envelope 始终以显式
`ErrorCode` 和 diagnostic string 开头。Client 会拒绝错误 opcode、错误 Request ID、malformed
Response body、不支持的 version 和 oversized Response。

## Registry Model

`NodeRecord` 包含 `node_id`、唯一且单调分配的 `session_id`、唯一 `node_name`、Primary/
Heartbeat connection ID、最后一次 monotonic Heartbeat 时间、Liveness state，以及拥有的
Publisher/Subscriber Endpoint ID set。Daemon 运行期间不会复用 ID。Heartbeat 必须同时匹配
Connection binding、Node ID 和 Session ID；PID 不具备 identity authority。

`TopicRecord` 包含 `topic_id`、名称、`type_name`、`type_hash`、`transport_type`、协商后
的最大消息大小、至多一个 Publisher Endpoint 和零到多个 Subscriber Endpoint。正常拆除
Endpoint 后，空 Topic 会被移除。

Publisher 和 Subscriber Endpoint 是不同的 record，均包含 `endpoint_id`、`node_id` 和
`topic_id`。Subscriber 还记录 Data Socket path、消息大小上限和 SHM Queue descriptor；SHM
Publisher 记录 Pool descriptor。这些值是 Discovery metadata，不是 Control Plane payload data。
Registry 从不 map 任何一个 SHM object。

## 类型兼容与 Publisher 规则

只有 Topic name、`type_name`、`type_hash` 和 `transport_type` 全部精确匹配时，Registry
才认为双方兼容。它不了解消息字段，也不实现 IDL 或 Schema converter。Schema mismatch 返回
`TypeMismatch`；UDS/SHM mismatch 返回 `TransportMismatch`；第二个 active Publisher 返回
`DuplicatePublisher`。

状态模型保留 N 个 Subscriber。SHM resolve 返回所有 Subscriber 及其独立 Queue capacity/policy，
并为每个 Subscriber 建立一条直接 metadata UDS。Copied direct UDS mode 继续选择第一个兼容
Endpoint。

## Discovery 流程

Subscriber-first 启动流程如下：

```text
Subscriber binds its data socket and creates its SHM queue
  -> REGISTER_NODE
  -> SUBSCRIBE_TOPIC(data socket, type, queue descriptor)
Publisher REGISTER_NODE
  -> ADVERTISE_TOPIC(type)
  -> RESOLVE_ENDPOINT (pool descriptor + N subscriber sockets/queues)
  -> connect directly to subscriber data sockets
  -> send copied UDS data, or enqueue shared-pool handles and send wakes
```

Publisher-first 启动时，`ADVERTISE_TOPIC` 立即成功。第一次 `publish()` 发送
`RESOLVE_ENDPOINT`；没有 Subscriber 时，Daemon 会保留该请求。之后兼容的 subscription
完成 pending Request，原 Publisher 无需重启即可建立连接。Discovery 成功后才开始对 Publisher
payload sequence 编号。

SHM Publisher 至少建立一条 Connection 后，最多复用最后一次兼容 Discovery 结果 1 ms，不再
每次 publish 都同步 resolve。空 Connection set、Data Socket/Queue 故障、disconnect 或
Peer-death event 会立即使刷新窗口失效。已有其他 Subscriber 保持连接时加入的新 Subscriber，
会在下一次有界刷新中被发现。UDS Discovery 行为不变。

正常析构 Endpoint 会发送 unadvertise/unsubscribe，最后一个 Session Owner 会 unregister Node。
Primary Control EOF/HUP 会立即移除 Node 和 Endpoint。如果 Primary Socket 保持打开但 Heartbeat
停止，monotonic state machine 会从 ALIVE 进入 SUSPECTED，最终进入 terminal DEAD。Cleanup
关闭配套 Heartbeat connection，移除精确 Registry record，unlink 已注册的 Pool/Queue/Socket
name，repair/close dead Subscriber Queue，并发送 Peer event。重复 Cleanup 可接受缺失的 record
和 name，因此是安全的。

## mwctl

`mwctl` 是普通 `RegistryClient`，无法访问 Daemon memory 或 side file：

```bash
./build/bin/mwctl node list
./build/bin/mwctl topic list
./build/bin/mwctl topic info /ping
./build/bin/mwctl stats
```

使用非默认 Control Socket 时，在 resource name 前传入 `--registry PATH`。Node 和 Topic list
按名称排序，保证输出确定性。`node list` 包含 `ALIVE` 或 `SUSPECTED`；DEAD record 已被移除。
`stats` 报告当前 Node、Topic、Publisher、Subscriber 和 Endpoint 数量，以及累计 Heartbeat
receive、suspected-transition 和 dead-node counter。它是有界的 Registry snapshot，不是通用的
per-publication metrics exporter。

## 已知限制

- 仅支持 Linux 单机 UDS，不支持分布式 Discovery。
- Heartbeat 默认 interval 为 250 ms，suspect timeout 为 750 ms，dead timeout 为 1500 ms；
  只有满足 interval < suspect < dead 时，Daemon CLI 和 `RegistryConfig` 才允许覆盖。
- `RegistryClient` 的应用调用是同步的；Publisher 可以无限等待第一个兼容 Subscriber，同时
  其独立 Heartbeat connection 仍保持响应。
- Registry Session 设计为由应用串行调用，不支持并发 Request multiplexing。
- SHM 和 UDS Discovery 返回所有当前 Subscriber；一个 active Publisher 会把一个逻辑消息
  fan-out 到每个独立 Endpoint。
- Registry 仅在执行有界 robust close/repair 时 map 已注册的 dead-Subscriber Queue。它从不创建
  Data Plane storage、转发 payload 或扫描 namespace。
- Heartbeat connection 故障会终止该 Session 的续租；现有 Context 不会自动重连 Registry daemon。
- Discovery 仅限单机，不实现分布式 Middleware。
