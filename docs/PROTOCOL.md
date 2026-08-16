# 协议

## 范围

项目包含三个明确的 Protocol surface：Registry Control stream、copied UDS Data frame，以及
SHM Queue notification/release metadata。Control 和 UDS wire format 从不使用 C++ object layout。

## Control Header

Registry Client 使用 `AF_UNIX` `SOCK_STREAM`。每个 frame 以 16-byte big-endian Header 开头：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `0x4D574332` (`MWC2`) |
| 4 | 2 | protocol version `5` |
| 6 | 2 | opcode |
| 8 | 4 | request ID |
| 12 | 4 | payload size |

Payload 上限为 64 KiB。Response 复用 request ID，并以 `ErrorCode` 和 diagnostic string 开头。
`RegistryClient` 会拒绝错误的 opcode、错误的 request ID、malformed body、不支持的 version、
oversized frame，以及 operation-specific body 之后的多余字节。

## Operation

| Operation | Body 用途 |
| --- | --- |
| `REGISTER_NODE` / `UNREGISTER_NODE` | 创建/移除唯一的活动 Node session |
| `ADVERTISE_TOPIC` / `UNADVERTISE_TOPIC` | 注册/移除唯一的 active Publisher |
| `SUBSCRIBE_TOPIC` / `UNSUBSCRIBE_TOPIC` | 注册/移除一个 Subscriber Endpoint |
| `RESOLVE_ENDPOINT` | 返回兼容的 Subscriber Socket、Queue descriptor 和 Pool descriptor |
| `LIST_NODES` | 返回活动 Node ID、名称和 ALIVE/SUSPECTED 状态 |
| `LIST_TOPICS` | 返回当前 Topic ID 和名称 |
| `QUERY_TOPIC` | 返回类型、Transport、Size、Endpoint 数量和 Pool metadata |
| `QUERY_STATS` | 返回当前 Registry object 数量和累计 Liveness counter |
| `ATTACH_HEARTBEAT` / `HEARTBEAT` | 绑定/续租 Node session，并传递 Peer-death event |

新增 `QUERY_STATS` 扩展了 Protocol v5，但没有改变任何现有 Body encoding。

## 注册与 Discovery

Subscriber-first：

```text
subscriber REGISTER_NODE
  -> create listener and SHM queue when selected
  -> SUBSCRIBE_TOPIC
publisher REGISTER_NODE
  -> create/advertise pool when SHM
  -> ADVERTISE_TOPIC
  -> RESOLVE_ENDPOINT
  -> connect directly to subscribers
```

Publisher-first 的 advertisement 会成功。第一次 resolve 若没有兼容 Subscriber，Registry 会
保留请求，直到 Subscriber 注册。必须精确匹配 `topic_name`、`type_name`、`type_hash` 和
Transport；第二个 active Publisher 会被拒绝。

## UDS Data Frame

Copied baseline 使用 24-byte `MW01` Header，后接恰好 `payload_size` 个字节：

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `0x4D573031` |
| 4 | 4 | payload size |
| 8 | 8 | publisher sequence |
| 16 | 8 | monotonic publish timestamp in ns |

Subscriber 会在多次调用之间保留 partial stream state，并在分配 owning payload vector 前校验
Size。允许 zero-size payload。

## SHM Handle 与 Notification

业务 payload 保留在 Publisher Pool 中。Queue entry 保存以下逻辑 Handle：

```text
pool_id
chunk_index
generation
payload_offset
```

Empty-to-nonempty Wake 是固定 272-byte big-endian metadata frame，包含 Pool descriptor 和
Queue ID。Release 是包含 Handle 的固定 32-byte frame。二者都不携带业务 payload。Ring Queue
是权威数据源，因此一个 Wake 可以对应多个 Handle，Subscriber 会以 nonblocking 方式 drain
多余的完整 Wake。

## Heartbeat 与 Recovery 消息

注册会同时返回 `node_id` 和单调分配的 `session_id`。独立 Connection 必须用这两个值完成
attach，才能发送 Heartbeat。Response 包含当前 Liveness state，以及面向活动 Endpoint 的有界
`PublisherDead` 或 `SubscriberDead` event。PID 从不作为 Session authority。

正常的 unadvertise/unsubscribe/unregister 消息显式移除所有权。EOF/HUP 或 lease death 会生成
相同的幂等 cleanup plan，其中包含精确的 Pool、Queue 和 Socket name。

## 安全检查

- 处理 Body 前校验固定 magic/version 和 payload 上限。
- 使用显式 big-endian integer/string encoding，并检查长度。
- 使用非零、单调递增的 request ID 关联 Request/Response。
- 不完整 frame 不会导致部分状态变更。
- 精确匹配 Topic/Type/Hash/Transport。
- 校验 Pool/Queue magic、version、size、ID、alignment 和 range。
- Generation 校验防止 stale release 影响已复用 Chunk。
- 未知 opcode 和 malformed stats/query body 返回明确 Protocol error。

详细状态行为见 [Control Plane](CONTROL_PLANE.md)，Data ownership 和 Layout 见
[内存模型](MEMORY_MODEL.md)。
