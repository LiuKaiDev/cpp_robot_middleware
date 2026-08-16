# Shared Memory Pool And Chunk Lifecycle

## Scope

Each SHM publisher owns one preallocated pool for its endpoint lifetime. Subscriber queues,
public loan/view APIs, and crash-time ownership repair operate around the same chunk lifecycle. See
[QUEUES_AND_LOANING.md](QUEUES_AND_LOANING.md) for the enqueue/reference protocol and public RAII
lifetimes.

## Pool Lifetime And Ownership

An SHM publisher generates a `/mw_p5_<pid>_<pool-id>` name, computes the complete checked layout,
advertises its descriptor through the registry, and creates the POSIX object with the existing
move-only `SharedMemoryRegion`. Creation performs one `shm_open`, one `ftruncate`, and one writable
`mmap`. The pool remains mapped until publisher destruction.

The publisher owns the SHM name, writable mapping, free lists, chunk state changes, reference-count
changes, reclamation, and final `shm_unlink`. A subscriber receives the name, pool ID, segment size,
layout version, and topic ID through discovery/wake metadata. It opens one read-only,
non-name-owning mapping on its first wake and retains it for its endpoint lifetime.

Normal publisher destruction closes data connections, unmaps, and unlinks the pool. Normal
subscriber destruction unmaps and unsubscribes. An unlink removes the name without invalidating an
already open mapping. If a publisher dies, the registry unlinks its exact advertised pool name and
notifies surviving subscribers. The daemon does not scan `/dev/shm` or infer ownership from PID.

## Configuration And Size Classes

`PublisherConfig::memory_pool` is a C++ configuration containing ordered
`MemoryPoolClassConfig { chunk_size, chunk_count }` entries. Defaults are:

| Capacity | Count |
| ---: | ---: |
| 256 B | 32 |
| 4 KiB | 16 |
| 64 KiB | 8 |
| 1 MiB | 4 |
| 4 MiB | 2 |

Allocation selects the smallest capacity greater than or equal to the requested payload. A 1000 B
request therefore uses a 4 KiB chunk. If that class has no free chunk, allocation immediately
returns `PoolExhausted`; it does not block, drop a message, or borrow from a larger class. A request
larger than the largest class returns `MessageTooLarge`.

## Segment Layout

The single segment contains:

```text
+-------------------------------+
| encoded PoolHeader (80 B)     |
+-------------------------------+
| SizeClassMetadata[] (24 B)    |
+-------------------------------+
| ChunkDirectoryEntry[] (32 B)  |
+-------------------------------+ 64-byte aligned
| ChunkHeader 0 (64 B)          |
| Payload capacity 0            |
+-------------------------------+ 64-byte aligned
| ChunkHeader 1 + payload       |
| ...                           |
+-------------------------------+
```

The explicitly encoded pool header contains magic, layout version, exact segment size, pool ID,
topic ID, owner PID, counts, and metadata/storage offsets. Each size-class record contains capacity,
first global chunk index, and count. Each directory record contains the chunk-header offset,
payload offset, capacity, and class index.

Layout construction uses checked addition, multiplication, and alignment. A subscriber validates
magic, version, descriptor identities, exact segment size, counts, metadata ranges, ordered size
classes, directory ranges, 64-byte alignment, non-overlap, chunk indices, and duplicated chunk
metadata before accessing payload bytes.

## Chunk Header And Handle

Each 64-byte aligned `ChunkHeader` contains lock-free atomic `state` and `ref_count`, payload size,
capacity, size-class index, generation, sequence, monotonic publish timestamp, topic ID, and pool ID.
A compile-time assertion requires `std::atomic<uint32_t>` to be always lock-free.

The fixed logical handle is:

```text
pool_id
chunk_index
generation
payload_offset
```

Mappings can have different virtual addresses in different processes. Equality therefore means
the four logical identity fields match, not that pointers compare equal. Generation changes on
every allocation and rejects a release for a prior use of the same index.

The shared-atomic implementation assumes the same supported Linux host, compiler ABI, and native
32-bit lock-free atomic representation for publisher and subscribers. Cross-platform shared-memory
ABI compatibility is outside the local-host v1 scope.

## Lifecycle And Free Lists

The normal state machine is:

```text
FREE -> LOANED -> PUBLISHED -> RELEASED -> FREE
```

Allocation removes one index from its publisher-local per-class free list and changes
`FREE -> LOANED`. The copy path writes the payload once; the public `LoanedSample` path exposes only
that chunk's payload area for direct application fill. Publication initializes the guard reference
and publishes the state.
The last valid release changes `PUBLISHED -> RELEASED`; explicit reclaim returns the index to the
same class and changes `RELEASED -> FREE`.

Only the publisher touches free lists. A publisher-local `std::mutex` serializes allocation,
lifecycle validation, reference changes, and reclamation. The retained vector capacities mean
free-list pop/push does not allocate chunk storage or allocate a payload in the hot path. There is
no lock-free free list, per-thread cache, or interprocess mutex.

Invalid transitions, allocation of a non-free chunk, publish outside `LOANED`, reclaim before zero
references, duplicate release, invalid index/offset/pool ID, and stale generation are rejected.

## Multi-Subscriber Sharing And Release

Registry control protocol v5 returns every compatible subscriber endpoint and queue descriptor
plus the publisher pool descriptor. The publisher connects one data UDS and maps one queue per
discovered subscriber. One publish does:

```text
allocate one chunk
copy application payload into that chunk once
set ref_count = connected subscriber count
enqueue the same fixed ChunkHandle into N subscriber queues
process fixed RELEASE frames asynchronously on later calls/destruction
decrement exactly once per connection
reclaim at ref_count == 0
```

Subscribers do not decrement the atomic directly. Each `SampleView` emits exactly one release on
destruction; the owning API copies through a temporary view. The publisher is the single refcount
writer and tracks outstanding handles per endpoint in vectors whose maximum is the configured
finite chunk count. A dead-subscriber event, socket disconnect, failed dispatch, or discovery
removal releases every still-tracked obligation exactly once. This includes a reference owned by a
`SampleView` that vanished with a killed subscriber process.

The copy API completes initial discovery before allocating. A loan may allocate before discovery,
but failed publication cancels it or reclaims it after the guard reference, so no chunk remains
stuck in `LOANED` or `PUBLISHED` merely because no endpoint accepted it.

## Queue Metadata Protocol

A queue wake is fixed at 272 bytes and carries pool descriptor plus queue ID. A release is fixed at
32 bytes and carries the logical chunk handle. Both are explicitly big-endian encoded and contain
no business payload. The `ChunkHandle` itself resides in the shared ring queue.

## Copy And Allocation Semantics

Ordinary `publish()` retains one application-buffer-to-SHM copy, and owning `ReceivedMessage`
copies from a temporary view. The Phase 5 loan-to-view path avoids middleware payload copies
between application fill and subscriber read. All paths continue to avoid per-message
`shm_open`/`ftruncate`/`mmap`/`munmap`/`shm_unlink` operations.

The chunk allocator performs no per-message payload allocation. Publisher connection bookkeeping,
wake/release polling containers, control-protocol strings/vectors, and each owning subscriber
message can still allocate ordinary process heap memory. The measured behavior and profiling
limits are documented in [BENCHMARK.md](BENCHMARK.md) and the Phase 8.1 report.

## Known Limitations

- No `eventfd` or `SCM_RIGHTS` optimization; UDS wakes remain in use.
- A live but indefinitely stalled subscriber is reclaimed only after its whole node misses the
  heartbeat dead timeout; there is no independent per-view lease.
- Registry loss is not recovered inside an existing context, and arbitrary pool-header corruption
  is not repaired.
- The ROS2 adapter retains serialization copies and does not extend the native loan/view lifetime.
- Reference performance is host-specific; see [BENCHMARK.md](BENCHMARK.md), not the pool design,
  for measured conclusions.
