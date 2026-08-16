# Protocol

## Scope

The project has three explicit protocol surfaces: the registry control stream, the copied UDS data
frame, and SHM queue notification/release metadata. C++ object layout is never used as the control
or UDS wire format.

## Control Header

Registry clients use `AF_UNIX` `SOCK_STREAM`. Every frame starts with a 16-byte, big-endian header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `0x4D574332` (`MWC2`) |
| 4 | 2 | protocol version `5` |
| 6 | 2 | opcode |
| 8 | 4 | request ID |
| 12 | 4 | payload size |

Payloads are limited to 64 KiB. Responses repeat the request ID and begin with an `ErrorCode` plus
a diagnostic string. `RegistryClient` rejects a wrong opcode, wrong request ID, malformed body,
unsupported version, oversized frame, and trailing operation-specific bytes.

## Operations

| Operation | Body purpose |
| --- | --- |
| `REGISTER_NODE` / `UNREGISTER_NODE` | Create/remove a unique live node session |
| `ADVERTISE_TOPIC` / `UNADVERTISE_TOPIC` | Register/remove the one active publisher |
| `SUBSCRIBE_TOPIC` / `UNSUBSCRIBE_TOPIC` | Register/remove one subscriber endpoint |
| `RESOLVE_ENDPOINT` | Return compatible subscriber sockets, queue descriptors, and pool descriptor |
| `LIST_NODES` | Return live node IDs, names, and ALIVE/SUSPECTED state |
| `LIST_TOPICS` | Return current topic IDs and names |
| `QUERY_TOPIC` | Return type, transport, size, endpoint counts, and pool metadata |
| `QUERY_STATS` | Return current registry object counts and lifetime liveness counters |
| `ATTACH_HEARTBEAT` / `HEARTBEAT` | Bind/renew a node session and deliver peer-death events |

Adding `QUERY_STATS` extends protocol v5 without changing any existing body encoding.

## Registration And Discovery

Subscriber-first:

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

Publisher-first advertisement succeeds. A first resolve with no compatible subscriber is held by
the registry until one subscribes. Exact `topic_name`, `type_name`, `type_hash`, and transport must
match. A second active publisher is rejected.

## UDS Data Frame

The copied baseline uses a 24-byte `MW01` header followed by exactly `payload_size` bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `0x4D573031` |
| 4 | 4 | payload size |
| 8 | 8 | publisher sequence |
| 16 | 8 | monotonic publish timestamp in ns |

The subscriber preserves partial stream state across calls and validates size before allocating the
owning payload vector. Zero-size payloads are valid.

## SHM Handle And Notification

Business payload stays in the publisher pool. Queue entries contain the logical handle:

```text
pool_id
chunk_index
generation
payload_offset
```

An empty-to-nonempty wake is a fixed 272-byte, big-endian metadata frame containing the pool
descriptor and queue ID. A release is a fixed 32-byte frame containing the handle. Neither carries
business payload. The ring queue is authoritative, so one wake may cover multiple handles and the
subscriber drains redundant complete wakes nonblockingly.

## Heartbeat And Recovery Messages

Registration returns both `node_id` and monotonically assigned `session_id`. A separate connection
must attach with both values before sending heartbeats. Responses contain the current liveness state
and bounded `PublisherDead` or `SubscriberDead` events targeted to live endpoints. PID is never the
session authority.

Normal unadvertise/unsubscribe/unregister messages remove ownership explicitly. EOF/HUP or lease
death produces the same idempotent cleanup plan with exact pool, queue, and socket names.

## Safety Checks

- Fixed magic/version and payload bound before body processing.
- Explicit big-endian integer/string encoding with checked lengths.
- Request/response correlation by nonzero, monotonically increasing request ID.
- No partial state mutation from incomplete frames.
- Exact topic/type/hash/transport compatibility.
- Pool/queue magic, version, size, ID, alignment, and range validation.
- Generation validation prevents a stale release from affecting a reused chunk.
- Unknown opcode and malformed stats/query bodies return explicit protocol errors.

Detailed state behavior is in [CONTROL_PLANE.md](CONTROL_PLANE.md); data ownership and layout are in
[MEMORY_MODEL.md](MEMORY_MODEL.md).
