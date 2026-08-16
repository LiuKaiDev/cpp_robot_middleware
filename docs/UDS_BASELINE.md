# Unix Domain Socket Pub/Sub Baseline

## Scope

Phase 1 provides a copied-payload, Linux-local baseline for one publisher, one subscriber, and one
explicitly configured topic. It exists to validate the public API and establish a correctness
baseline for later transports. It is not a shared-memory or zero-copy transport.

## Connection Model

The subscriber is the Unix Domain Socket server:

```text
socket(AF_UNIX, SOCK_STREAM)
  -> bind(socket_path)
  -> listen()
  -> poll()/accept4()
```

The publisher is the client:

```text
socket(AF_UNIX, SOCK_STREAM)
  -> connect(socket_path)
  -> publish()
```

The socket path is supplied through `PublisherConfig` and `SubscriberConfig`; it is not hardcoded
inside the transport. Phase 1 has no registry or discovery service.

## Frame Layout

Every message is encoded as a fixed 24-byte header followed by exactly `payload_size` bytes:

| Offset | Size | Field | Encoding |
| --- | ---: | --- | --- |
| 0 | 4 | magic (`0x4D573031`, ASCII `MW01`) | big-endian |
| 4 | 4 | payload size | big-endian |
| 8 | 8 | publisher sequence | big-endian |
| 16 | 8 | publish timestamp in nanoseconds | big-endian |

The implementation encodes each field explicitly and does not use C++ structure layout as the wire
format. Before allocating a payload buffer, the subscriber validates the magic and configured
maximum message size. A zero-byte payload is valid and still carries a complete header.

## Sequence And Timestamp

Each publisher instance starts at sequence 1 and increments only after a complete frame is written.
The timestamp is captured by `std::chrono::steady_clock` when `publish()` builds the frame. It is a
monotonic nanosecond value, not wall-clock time.

## Partial I/O

Publisher writes use an `EINTR`-safe `writeAll()` loop and `MSG_NOSIGNAL`. The subscriber uses a
nonblocking accepted socket plus `poll()` and retains partial header and payload state between API
calls. Consequently, `take()` can return without discarding an incomplete frame, and
`waitAndTake()` applies one deadline to accept and receive work.

## Resource Ownership

- `UniqueFd` is move-only and closes its descriptor in its destructor.
- A `Publisher` owns one connected socket descriptor.
- A `Subscriber` owns one listening descriptor and, while connected, one accepted descriptor.
- `UnixListener` owns the pathname after a successful bind. Its destructor first closes the
  listening descriptor and then unlinks the pathname.
- Startup does not blindly unlink an existing pathname. An active or stale pathname causes bind to
  fail. In registry mode, Phase 6 unlinks the exact registered subscriber socket after node death;
  direct mode has no registry authority and retains the original manual stale-path boundary.

## Disconnect And Reconnect

EOF with no partial frame is reported as `ConnectionLost`. EOF after a partial header or payload is
reported as `InvalidFrame`. Either case closes only the accepted connection and resets its decoder;
the listening socket remains active. A later publisher instance can connect and communicate with
the same subscriber. Publisher-side peer closure is returned from `publish()` and cannot terminate
the process through `SIGPIPE`.

Bad magic, oversized frames, and truncated frames are deterministic connection-level failures. No
untrusted payload allocation occurs before header validation.

## Thread And Process Model

Direct mode has no middleware worker threads. A registry-enabled context has the Phase 6
control-only heartbeat thread. `publish()` sends on the caller's thread.
`take()`/`waitAndTake()` poll, accept, and receive on the caller's thread. Publisher and subscriber
are intended to live in separate processes; tests also exercise the same transport in one process.

## Demo

Start the subscriber first:

```bash
./build/bin/mw_ping_subscriber \
  --socket /tmp/mw_phase1.sock \
  --count 100000 \
  --size 64
```

Then run the publisher:

```bash
./build/bin/mw_ping_publisher \
  --socket /tmp/mw_phase1.sock \
  --count 100000 \
  --size 64
```

Both programs also accept `--timeout-ms`; it controls each subscriber wait and is parsed but unused
by the synchronous publisher.

## Known Limitations

- One publisher, one subscriber, and one explicitly configured topic.
- No registry, discovery, topic/type negotiation, or multi-publisher ordering.
- Payloads are copied through the kernel socket path.
- No subscriber queue, backpressure policy, persistence, retransmission, or worker thread.
- The direct Phase 1 path has no shared memory, memory pool, loaned sample, or automatic crash
  cleanup. Registry-mode lifecycle recovery is documented separately.
- No ROS2 adapter or benchmark framework.
- A pathname left by an unclean subscriber exit must be removed by the operator in Phase 1.
