# Data Plane

## Scope

Phase 3 provides two independently selectable, Linux-local payload paths:

- `TransportType::UnixDomainSocket` is the preserved copied-payload baseline.
- `TransportType::SharedMemory` is the registry-discovered POSIX SHM V1 transport.

The registry control plane remains UDS in both modes. SHM mode also retains the direct data UDS for
small locator and acknowledgement metadata. Business payload bytes do not travel through UDS in
SHM mode.

## UDS Baseline

The UDS path is unchanged from Phase 1. A publisher sends the explicitly encoded 24-byte `MW01`
header followed by exactly `payload_size` bytes. The subscriber incrementally receives the stream
and returns an owning `ReceivedMessage`. Direct mode supports this baseline; registry mode can also
select it with `TransportType::UnixDomainSocket`.

## SHM V1 Architecture

```text
                         mw_registryd
                       UDS control plane
                         /          \
                    Publisher     Subscriber
                        |              ^
  SHM object: header + payload          |
                        +-- UDS locator-+
                        +<---- ACK -----+
```

`publish()` creates one object for one in-flight message, copies the application buffer into it,
and sends a fixed-size locator. `waitAndTake()` opens and maps that object, validates it, copies the
payload into the existing owning message API, and sends an ACK. The publisher then unlinks the
object. There are no middleware data worker threads.

## SharedMemoryRegion Ownership

`SharedMemoryRegion` is move-only. Every instance owns its fd and mapping and closes/unmaps them in
its destructor. Name ownership is separate:

- The publisher creator uses `shm_open(O_CREAT | O_EXCL | O_RDWR)`, `ftruncate`, and writable
  `mmap`; it owns the POSIX name.
- The subscriber uses `shm_open(O_RDONLY)` and read-only `mmap`; it never owns or unlinks the name.
- A successful ACK allows the publisher to call `shm_unlink`. If notification, ACK, validation, or
  a normal socket operation fails, the publisher region destructor performs best-effort unlink.
- The subscriber mapping lasts only through validation and the copy into `ReceivedMessage`.

Unlink removes the name but does not invalidate mappings that are already open. Crash-time orphan
recovery after an abrupt publisher `SIGKILL` is outside Phase 3 and belongs to Phase 6.

## Naming And Segment Layout

Names have the form `/mw_p3_<publisher-pid>_<endpoint-id>_<sequence>`. They contain no raw topic
text, satisfy POSIX naming rules, and combine process, endpoint, and message identity.

Each segment is exactly:

```text
+----------------------------+ offset 0
| SharedMemoryHeader (48 B)  |
+----------------------------+ offset 48
| Payload (payload_size B)   |
+----------------------------+
```

The 48-byte, big-endian header contains magic `MWS3`, version, header size, segment size, payload
size, sequence, monotonic publish timestamp, and registry topic ID. It is encoded field by field;
C++ structure layout is not a cross-process ABI. Validation requires the configured payload bound,
exact mapped segment size, exact `header_size + payload_size`, and equality with notification
metadata before any payload access.

## Notification And ACK

An SHM notification is a fixed 248-byte UDS frame with magic `MWN3`, version 1, kind
`Notification`, bounded name length, fixed frame size, segment/payload sizes, sequence, timestamp,
topic ID, and a zero-padded 192-byte name field. A 1 KB payload and a 4 MB payload therefore send
the same number of UDS notification bytes. No application payload field exists in this frame.

The ACK is a fixed 16-byte `MWN3` frame with version, kind `Ack`, and sequence. One publisher waits
for one matching subscriber ACK before unlink. This is deliberately a one-publisher/one-subscriber
normal-lifecycle protocol, not reference counting.

## Publish And Consume Ordering

The publisher writes the complete encoded header and payload before its UDS notification write.
The subscriber never opens or consumes the region before receiving that notification. The UDS
protocol ordering provides the sequencing boundary needed by this same-host V1 path without adding
an atomic chunk lifecycle.

The consume sequence is: receive notification, validate bounds and topic, open/map read-only,
validate the mapped header and all duplicated metadata, copy the payload, send ACK, then unmap.
The publish sequence is: validate, create/truncate/map, write header and payload, notify, wait for
matching ACK, unlink, then unmap/close.

## Error Handling

Configuration rejects unknown transports and direct SHM mode. The registry rejects UDS/SHM
mismatch. The codecs reject bad magic, unsupported versions, wrong frame kind, truncation,
oversized or invalid names, inconsistent sizes, wrong topic/sequence/timestamp, and payloads above
the negotiated bound. Checked bounds prevent segment overflow and mapping beyond the object.

Missing SHM objects, other `shm_open`/`ftruncate`/`mmap` failures, invalid mapped headers, ACK
timeouts, socket disconnects, and explicit unlink failures have distinct public error outcomes.
Data connections are reset after protocol or socket failures. Destructors remain noexcept and make
best-effort cleanup.

## Copy Semantics And Limitations

SHM V1 is not zero-copy. It has an application-buffer-to-SHM copy and, because the public API still
returns an owning `ReceivedMessage`, a mapped-SHM-to-vector copy. No performance conclusion is made
in Phase 3.

There is no memory pool, reusable chunk pool, free list, multi-subscriber payload sharing,
reference-counted message lifecycle, subscriber queue, backpressure policy, loaned sample/view,
`eventfd`, `SCM_RIGHTS`, heartbeat, crash-time orphan cleanup, ROS2 adapter, or benchmark result.
