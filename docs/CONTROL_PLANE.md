# Registry Control Plane

## Scope And Separation

The final v1 control plane provides registry-based discovery, transport/pool/queue metadata,
liveness sessions, read-only inspection, and exact crash cleanup on one Linux host while keeping
control and payload responsibilities separate:

```text
Context / Publisher / Subscriber / mwctl
                 |
                 | Control Protocol
                 v
       mw_registryd control UDS

Publisher --------------------------> Subscriber
       UDS payload frame, or queue wake/release UDS
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
- Each session owns a second control connection and one RAII heartbeat thread. The primary
  connection remains available for synchronous calls while the heartbeat connection periodically
  renews the node lease and receives peer-death events.

The daemon uses one event-loop thread. Applications have one control-only heartbeat thread; all
data-plane work, discovery, publish, receive, queue repair, and reference repair remains on caller
threads.

## Control Socket And Header

The control plane uses `AF_UNIX`, `SOCK_STREAM`. Each frame begins with this logical 16-byte header:

| Offset | Size | Field | Encoding |
| --- | ---: | --- | --- |
| 0 | 4 | magic (`0x4D574332`, `MWC2`) | big-endian |
| 4 | 2 | protocol version (`5`) | big-endian |
| 6 | 2 | opcode | big-endian |
| 8 | 4 | request ID | big-endian |
| 12 | 4 | payload size | big-endian |

Version 5 retains pool and subscriber queue descriptors and adds node session IDs, heartbeat attach
and renewal, liveness state, and bounded peer-death events. Resolve
returns a counted list of all compatible subscriber endpoint IDs, socket paths, size bounds, and
queue descriptors. The header is encoded field by field; a C++ structure is never sent as the wire
ABI.
Payloads are bounded to 64 KiB before allocation. The server retains incomplete stream input until
a complete header and declared payload are present. Bad magic, unsupported version, oversized
payload, unknown opcode, malformed payload, and truncated connections cannot dispatch a partial
state change.

## Opcodes And Responses

The current protocol defines these operations:

| Opcode | Responsibility |
| --- | --- |
| `REGISTER_NODE` / `UNREGISTER_NODE` | Create or cleanly remove node identity and owned endpoints |
| `ADVERTISE_TOPIC` / `UNADVERTISE_TOPIC` | Create or remove the one active publisher |
| `SUBSCRIBE_TOPIC` / `UNSUBSCRIBE_TOPIC` | Create or remove a subscriber data endpoint |
| `RESOLVE_ENDPOINT` | Return compatible subscriber sockets, queue descriptors, and SHM pool metadata |
| `LIST_NODES` | Return sorted live registry node records |
| `LIST_TOPICS` | Return sorted live topic records |
| `QUERY_TOPIC` | Return type, size, publisher count, and subscriber count |
| `QUERY_STATS` | Return current object counts and lifetime liveness counters |
| `ATTACH_HEARTBEAT` | Bind a dedicated connection to one node/session identity |
| `HEARTBEAT` | Renew the lease and return liveness plus bounded peer-death events |
| `RESPONSE` | Carry an error code, message, and operation-specific body |

Every request receives the same `request_id` in its response. A response envelope always starts
with an explicit `ErrorCode` and diagnostic string. The client rejects the wrong opcode, wrong
request ID, malformed response body, unsupported version, and oversized response.

## Registry Models

A `NodeRecord` contains `node_id`, unique monotonically assigned `session_id`, unique `node_name`,
primary and heartbeat connection IDs, last monotonic heartbeat time, liveness state, and sets of
owned publisher/subscriber endpoint IDs. IDs are not reused by the running daemon. Heartbeats must
match all three of connection binding, node ID, and session ID; PID is not an identity authority.

A `TopicRecord` contains `topic_id`, name, `type_name`, `type_hash`, `transport_type`, negotiated
maximum message size, at most one publisher endpoint, and zero or more subscriber endpoints. Empty
topics are removed during clean endpoint teardown.

Publisher and subscriber endpoints are distinct records with `endpoint_id`, `node_id`, and
`topic_id`. A subscriber additionally records its data socket path, message-size bound, and SHM
queue descriptor. An SHM publisher records its pool descriptor. These values are discovery
metadata, not control-plane payload data. The registry never maps either SHM object.

## Type Compatibility And Publisher Rule

The registry treats a pair as compatible only when topic name, `type_name`, `type_hash`, and
`transport_type` all match exactly. It does not understand message fields and does not implement an
IDL or schema converter. A schema mismatch returns `TypeMismatch`; a UDS/SHM mismatch returns
`TransportMismatch`; a second active publisher returns `DuplicatePublisher`.

The state model retains N subscribers. Phase 5 SHM resolution returns all of them with independent
queue capacities and policies, and establishes one direct metadata UDS per subscriber. The copied
Phase 1 UDS baseline continues selecting the first compatible endpoint.

## Discovery Flow

Subscriber-first startup proceeds as follows:

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

For publisher-first startup, `ADVERTISE_TOPIC` succeeds immediately. The first `publish()` sends a
`RESOLVE_ENDPOINT` request, which the daemon holds when no subscriber exists. A later compatible
subscription completes that pending request; the original publisher then connects without being
restarted. Publisher payload sequence numbering begins only after discovery succeeds.

After an SHM publisher has at least one established connection, it reuses the last compatible
discovery result for at most 1 ms instead of synchronously resolving on every publication. An empty
connection set, data-socket/queue failure, disconnect, or peer-death event invalidates that refresh
window immediately. A subscriber joining while other subscribers remain connected is discovered on
the next bounded refresh. UDS discovery behavior is unchanged.

Normal endpoint destruction sends unadvertise/unsubscribe. The last session owner unregisters the
node. Primary control EOF/HUP immediately removes the node and endpoints. If the primary socket
stays open but heartbeats stop, the monotonic state machine changes ALIVE to SUSPECTED and then
terminal DEAD. Cleanup closes the companion heartbeat connection, removes exact registry records,
unlinks registered pool/queue/socket names, repairs/closes dead subscriber queues, and emits peer
events. Repeating cleanup is harmless because missing records and names are accepted.

## mwctl

`mwctl` is a normal `RegistryClient`; it has no access to daemon memory or side files:

```bash
./build/bin/mwctl node list
./build/bin/mwctl topic list
./build/bin/mwctl topic info /ping
./build/bin/mwctl stats
```

Use `--registry PATH` before the resource name for a non-default control socket. Node and topic
lists are sorted by name for deterministic output. `node list` includes `ALIVE` or `SUSPECTED`;
DEAD records have already been removed. `stats` reports current node, topic, publisher, subscriber,
and endpoint counts plus lifetime heartbeat-receive, suspected-transition, and dead-node counters.
It is a bounded registry snapshot, not a general per-publication metrics exporter.

## Known Limitations

- Linux single-host UDS only; no distributed discovery.
- Heartbeat defaults are 250 ms interval, 750 ms suspect timeout, and 1500 ms dead timeout; the
  daemon CLI and `RegistryConfig` may override them only when interval < suspect < dead.
- `RegistryClient` application calls are synchronous, and a publisher can wait indefinitely for
  its first compatible subscriber while its dedicated heartbeat connection remains responsive.
- Registry sessions are intended to be called serially by an application; concurrent request
  multiplexing is not implemented.
- SHM and UDS discovery return all current subscribers; one active publisher fans out one logical
  message to each independent endpoint.
- The registry maps a registered dead-subscriber queue only for bounded robust close/repair. It
  never creates data-plane storage, forwards payloads, or scans namespaces.
- A heartbeat connection failure ends renewal for that session; automatic registry-daemon
  reconnection within an existing context is not implemented.
- Discovery remains single-host and does not implement distributed middleware.
