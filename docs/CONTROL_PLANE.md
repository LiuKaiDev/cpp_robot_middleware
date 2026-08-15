# Registry Control Plane

## Scope And Separation

Phase 2 introduced registry-based discovery on one Linux host. Phase 3 extends endpoint metadata
with a transport type while keeping control and payload responsibilities separate:

```text
Context / Publisher / Subscriber / mwctl
                 |
                 | Control Protocol
                 v
       mw_registryd control UDS

Publisher --------------------------> Subscriber
       UDS payload frame, or SHM locator/ACK UDS
```

`mw_registryd` stores identities and endpoint metadata. It does not proxy or copy user payloads.
The default control pathname is `/tmp/mw_registry.sock`; `RegistryConfig`, `mw_registryd --socket`,
and `mwctl --registry` can select another path.

## Registry Architecture

- `ControlProtocol` explicitly encodes headers, typed payload fields, and response envelopes.
- `RegistryState` owns the node, topic, publisher, and subscriber maps and applies matching rules.
  It has no socket I/O.
- `RegistryServer` owns the listening socket, accepted control connections, `epoll` descriptor,
  partial input/output buffers, pending discovery requests, and `RegistryState`.
- `RegistryClient` sends synchronous correlated requests for middleware contexts and `mwctl`.
- A registry-enabled `Context` owns a shared `RegistrySession`; endpoints retain that session until
  their own destruction, so endpoint cleanup remains possible if the original Context is gone.

The daemon uses one event-loop thread. Applications do not gain a middleware worker thread:
control calls, discovery waits, publish, and receive all run on caller threads.

## Control Socket And Header

The control plane uses `AF_UNIX`, `SOCK_STREAM`. Each frame begins with this logical 16-byte header:

| Offset | Size | Field | Encoding |
| --- | ---: | --- | --- |
| 0 | 4 | magic (`0x4D574332`, `MWC2`) | big-endian |
| 4 | 2 | protocol version (`2`) | big-endian |
| 6 | 2 | opcode | big-endian |
| 8 | 4 | request ID | big-endian |
| 12 | 4 | payload size | big-endian |

Version 2 adds endpoint transport metadata to advertise, subscribe, discovery, and topic-query
payloads. The header is encoded field by field; a C++ structure is never sent as the wire ABI.
Payloads are bounded to 64 KiB before allocation. The server retains incomplete stream input until
a complete header and declared payload are present. Bad magic, unsupported version, oversized
payload, unknown opcode, malformed payload, and truncated connections cannot dispatch a partial
state change.

## Opcodes And Responses

Phase 2 defines only the operations it uses:

| Opcode | Responsibility |
| --- | --- |
| `REGISTER_NODE` / `UNREGISTER_NODE` | Create or cleanly remove node identity and owned endpoints |
| `ADVERTISE_TOPIC` / `UNADVERTISE_TOPIC` | Create or remove the one active publisher |
| `SUBSCRIBE_TOPIC` / `UNSUBSCRIBE_TOPIC` | Create or remove a subscriber data endpoint |
| `RESOLVE_ENDPOINT` | Return a compatible subscriber data socket to a publisher |
| `LIST_NODES` | Return sorted live registry node records |
| `LIST_TOPICS` | Return sorted live topic records |
| `QUERY_TOPIC` | Return type, size, publisher count, and subscriber count |
| `RESPONSE` | Carry an error code, message, and operation-specific body |

Every request receives the same `request_id` in its response. A response envelope always starts
with an explicit `ErrorCode` and diagnostic string. The client rejects the wrong opcode, wrong
request ID, malformed response body, unsupported version, and oversized response.

## Registry Models

A `NodeRecord` contains `node_id`, unique `node_name`, owning control connection, and sets of owned
publisher/subscriber endpoint IDs. IDs are monotonically assigned and are not reused by the running
daemon.

A `TopicRecord` contains `topic_id`, name, `type_name`, `type_hash`, `transport_type`, negotiated
maximum message size, at most one publisher endpoint, and zero or more subscriber endpoints. Empty
topics are removed during clean endpoint teardown.

Publisher and subscriber endpoints are distinct records with `endpoint_id`, `node_id`, and
`topic_id`. A subscriber additionally records its Phase 1 `data_socket_path` and message-size
bound. That pathname is discovery metadata, not a control transport.

## Type Compatibility And Publisher Rule

The registry treats a pair as compatible only when topic name, `type_name`, `type_hash`, and
`transport_type` all match exactly. It does not understand message fields and does not implement an
IDL or schema converter. A schema mismatch returns `TypeMismatch`; a UDS/SHM mismatch returns
`TransportMismatch`; a second active publisher returns `DuplicatePublisher`.

The state model retains N subscribers. The Phase 2 copied-payload data plane still establishes one
Phase 1 connection selected by discovery; multi-subscriber payload fan-out belongs to the later
message-lifecycle phases.

## Discovery Flow

Subscriber-first startup proceeds as follows:

```text
Subscriber binds its data socket
  -> REGISTER_NODE
  -> SUBSCRIBE_TOPIC(data socket, type)
Publisher REGISTER_NODE
  -> ADVERTISE_TOPIC(type)
  -> RESOLVE_ENDPOINT
  -> connect directly to subscriber data socket
  -> send Phase 1 frames and payloads
```

For publisher-first startup, `ADVERTISE_TOPIC` succeeds immediately. The first `publish()` sends a
`RESOLVE_ENDPOINT` request, which the daemon holds when no subscriber exists. A later compatible
subscription completes that pending request; the original publisher then connects without being
restarted. Publisher payload sequence numbering begins only after discovery succeeds.

Normal endpoint destruction sends unadvertise/unsubscribe. The last session owner unregisters the
node. RAII wrappers close descriptors, and listener owners unlink paths after clean shutdown.

## mwctl

`mwctl` is a normal `RegistryClient`; it has no access to daemon memory or side files:

```bash
./build/bin/mwctl node list
./build/bin/mwctl topic list
./build/bin/mwctl topic info /ping
```

Use `--registry PATH` before the resource name for a non-default control socket. Node and topic
lists are sorted by name for deterministic output.

## Known Limitations

- Linux single-host UDS only; no distributed discovery.
- No heartbeat, dead-process detection, crash cleanup, or stale pathname reclamation. Abruptly
  disconnected node records may remain until daemon restart; this is reserved for Phase 6.
- `RegistryClient` calls are synchronous, and a publisher can wait indefinitely for its first
  compatible subscriber.
- Registry sessions are intended to be called serially by an application; concurrent request
  multiplexing is not implemented.
- Discovery chooses one registered subscriber for both current one-to-one payload paths.
- The registry never creates, maps, unlinks, or forwards shared-memory payload objects.
- No memory pool, subscriber queue, backpressure, loaned sample, ROS2 adapter, or benchmark
  framework is implemented.
