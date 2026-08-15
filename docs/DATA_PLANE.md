# Data Plane

## Scope

Phase 5 retains two independently selectable, Linux-local payload transports and adds copy/loan
choices within SHM:

- `TransportType::UnixDomainSocket` is the copied Phase 1 baseline.
- `TransportType::SharedMemory` is the registry-discovered preallocated POSIX SHM pool transport.

The registry control plane remains UDS in both modes. SHM mode also retains direct publisher to
subscriber UDS connections for fixed wake/release metadata. Business payload bytes do not travel
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
                        +-- queues -----+--+
                        +-- UDS wakes --+--+
                        +<- RELEASEs ---+--+
```

The publisher creates and maps one pool at endpoint startup. Every subscriber creates one bounded
ring queue and the publisher maps it after discovery. `publish()` allocates a preexisting chunk and
copies the application buffer once; `loan()` instead lets the application fill the selected chunk
directly. Publication enqueues the same logical handle into every accepting subscriber queue.
`waitAndTakeView()` reads the mapped payload directly, while `waitAndTake()` retains the compatible
owning copy. The publisher reclaims the chunk after all endpoint references are released. There are
no middleware data worker threads.

## SharedMemoryRegion Ownership

`SharedMemoryRegion` remains the only mmap RAII wrapper. Every instance owns its fd and mapping and
closes/unmaps them in its destructor. Name ownership is separate:

- The publisher performs `shm_open(O_CREAT | O_EXCL | O_RDWR)`, `ftruncate`, and writable `mmap`
  once for the complete pool; it owns the POSIX name.
- A subscriber performs `shm_open(O_RDONLY)` and read-only `mmap` once for that publisher pool. It
  retains the non-name-owning mapping across messages and owns a separate read-write queue mapping.
- The publisher opens each discovered subscriber queue read-write but does not own its SHM name.
- The publisher region destructor performs best-effort normal `shm_unlink`.

Unlink removes the name but does not invalidate mappings that are already open. Crash-time orphan
and queue/reference repair after abrupt process death are outside Phase 5 and belong to Phase 6.

## Naming And Segment Layout

Pool names have the form `/mw_p5_<publisher-pid>_<pool-id>` and queue names use
`/mw_q5_<subscriber-pid>_<queue-id>`. They contain no raw topic text and do
not change per message.

The segment contains an explicitly encoded pool header, encoded size-class metadata, an encoded
chunk directory, and 64-byte-aligned native chunk headers plus fixed-capacity payload storage. See
[MEMORY_POOL.md](MEMORY_POOL.md) for the field layout, checked arithmetic, atomic ABI assumption,
state machine, and validation rules.

## Queue, Wake, And Release

A queue entry is one fixed logical chunk handle. An empty-to-nonempty wake is a fixed 272-byte UDS
frame containing the pool descriptor and queue ID; it carries no handle or business payload. A
release is fixed at 32 bytes and repeats the handle identity. One wake can cover multiple entries
because the queue, not the UDS stream, is the source of truth.

The logical handle contains pool ID, global chunk index, allocation generation, and payload offset.
Tests compare those fields across subscriber processes rather than comparing unrelated virtual
addresses.

## Publish And Consume Ordering

The publisher writes payload and non-atomic metadata, initializes one guard reference, then
release-stores `PUBLISHED`. It adds a tentative reference before each queue enqueue and releases
the guard only after all endpoint decisions. A subscriber acquire-loads state, validates duplicated
metadata, exposes a read-only view, and sends its release when that view is destroyed.

Subscribers do not modify the free list or decrement references. The publisher is the only
refcount writer and tracks outstanding handles per endpoint. Drop/timeout returns the tentative
new reference; `DROP_OLDEST` returns the displaced reference. The last release changes the chunk to
`RELEASED`; explicit reclaim returns it to its size-class free list as `FREE`.

## Error Handling

Configuration rejects unknown transports, direct SHM mode, invalid/unsorted/empty size classes,
zero queue depth/counts, invalid policy/timeouts, and layout overflow. Registry rejects type or
transport mismatch and invalid SHM pool/queue metadata. Subscribers reject bad magic/version,
inconsistent pool/topic/queue identities, out-of-range metadata, misalignment, invalid chunk
index/offset, stale generation, non-published state, and payload bounds before access.

Missing pools/queues, other `shm_open`/`ftruncate`/`mmap` failures, queue full/timeout/close, pool
exhaustion, socket disconnects, and explicit unlink failures have explicit outcomes. Loan publish
failure consumes or cancels the loan without leaving a LOANED chunk. If every endpoint rejects a
published chunk, releasing the publisher guard reclaims it.

## Copy Semantics And Limitations

Ordinary `publish()` has one application-buffer-to-SHM copy, and owning `ReceivedMessage` has one
mapped-SHM-to-vector copy. The verified loaned path avoids middleware payload copies between the
application filling `LoanedSample` and reading the same logical chunk through `SampleView`. This is
not a claim that UDS, all SHM APIs, or the middleware as a whole is zero-copy. No performance
conclusion is made in Phase 5.

`eventfd`, `SCM_RIGHTS`, heartbeat, dead-process detection, crash-time queue/refcount repair, ROS2,
and benchmark results remain unimplemented.
