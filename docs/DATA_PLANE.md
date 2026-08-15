# Data Plane

## Scope

Phase 4 retains two independently selectable, Linux-local payload paths:

- `TransportType::UnixDomainSocket` is the copied Phase 1 baseline.
- `TransportType::SharedMemory` is the registry-discovered preallocated POSIX SHM pool transport.

The registry control plane remains UDS in both modes. SHM mode also retains direct publisher to
subscriber UDS connections for small handle/release metadata. Business payload bytes do not travel
through UDS in SHM mode.

## UDS Baseline

The UDS path is unchanged from Phase 1. A publisher sends the explicitly encoded 24-byte `MW01`
header followed by exactly `payload_size` bytes. The subscriber incrementally receives the stream
and returns an owning `ReceivedMessage`. Direct mode supports this baseline; registry mode can also
select it with `TransportType::UnixDomainSocket`. This path selects one discovered subscriber.

## SHM Pool Architecture

```text
                         mw_registryd
                       UDS control plane
                         /          \
                    Publisher     Subscribers
                        |            ^  ^  ^
  SHM pool: metadata + reusable chunks |  |
                        +-- UDS handles-+--+
                        +<- RELEASEs ---+--+
```

The publisher creates and maps one pool at endpoint startup. `publish()` allocates a preexisting
chunk, copies the application buffer once, and sends the same fixed logical handle to N subscribers.
`waitAndTake()` retains one read-only pool mapping, validates the chunk, copies into the existing
owning message API, and sends a release. The publisher reclaims the chunk after all references are
released. There are no middleware data worker threads.

## SharedMemoryRegion Ownership

`SharedMemoryRegion` remains the only mmap RAII wrapper. Every instance owns its fd and mapping and
closes/unmaps them in its destructor. Name ownership is separate:

- The publisher performs `shm_open(O_CREAT | O_EXCL | O_RDWR)`, `ftruncate`, and writable `mmap`
  once for the complete pool; it owns the POSIX name.
- A subscriber performs `shm_open(O_RDONLY)` and read-only `mmap` once for that publisher pool. It
  retains the non-name-owning mapping across messages.
- The publisher region destructor performs best-effort normal `shm_unlink`.

Unlink removes the name but does not invalidate mappings that are already open. Crash-time orphan
and reference repair after abrupt process death are outside Phase 4 and belong to Phase 6.

## Naming And Segment Layout

Pool names have the form `/mw_p4_<publisher-pid>_<pool-id>`. They contain no raw topic text and do
not change per message.

The segment contains an explicitly encoded pool header, encoded size-class metadata, an encoded
chunk directory, and 64-byte-aligned native chunk headers plus fixed-capacity payload storage. See
[MEMORY_POOL.md](MEMORY_POOL.md) for the field layout, checked arithmetic, atomic ABI assumption,
state machine, and validation rules.

## Notification And Release

A pool notification is a fixed 272-byte UDS frame containing pool descriptor, logical chunk handle,
payload size, sequence, and timestamp plus a zero-padded 192-byte name field. A release is fixed at
40 bytes and repeats the handle identity and sequence. Neither contains application payload.

The logical handle contains pool ID, global chunk index, allocation generation, and payload offset.
Tests compare those fields across subscriber processes rather than comparing unrelated virtual
addresses.

## Publish And Consume Ordering

The publisher writes payload and non-atomic metadata, initializes the reference count, then
release-stores `PUBLISHED` before sending notifications. A subscriber acquire-loads state,
validates duplicated metadata, copies the payload, and sends its release.

Subscribers do not modify the free list or decrement references. The publisher is the only
refcount writer and tracks one outstanding reference per endpoint, so each accepted release or
normal connection loss decrements at most once. The last release changes the chunk to `RELEASED`;
explicit reclaim returns it to its size-class free list as `FREE`.

## Error Handling

Configuration rejects unknown transports, direct SHM mode, invalid/unsorted/empty size classes,
zero counts, and layout overflow. Registry rejects type or transport mismatch and invalid SHM pool
metadata. Subscribers reject bad magic/version, inconsistent pool/topic identities, out-of-range
metadata, misalignment, invalid chunk index/offset, stale generation, non-published state, and
payload bounds before access.

Missing pools, other `shm_open`/`ftruncate`/`mmap` failures, release timeouts, pool exhaustion,
socket disconnects, and explicit unlink failures have explicit error outcomes. A publisher waits
for initial discovery before allocating, so zero subscribers cannot strand a published chunk. If
all notification writes fail, all assigned references are removed and the chunk is reclaimed.

## Copy Semantics And Limitations

The SHM path is not zero-copy. It has one application-buffer-to-SHM copy and, because the public API
still returns owning `ReceivedMessage`, one mapped-SHM-to-vector copy in each subscriber. Multiple
subscribers share the one stored SHM payload but each still makes its final owning copy. No
performance conclusion is made in Phase 4.

There is no subscriber ring buffer/queue, backpressure policy, public loaned sample/view, `eventfd`,
`SCM_RIGHTS`, heartbeat, crash-time orphan/refcount repair, ROS2 adapter, or benchmark result.
