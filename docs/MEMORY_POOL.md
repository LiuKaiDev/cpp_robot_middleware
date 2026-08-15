# Shared Memory Pool And Chunk Lifecycle

## Scope

Phase 4 replaces the Phase 3 one-object-per-message SHM path with one preallocated pool for the
lifetime of each SHM publisher. It implements bounded size classes, reusable chunks, lifecycle
state, reference counting, and one-publisher-to-N-subscriber payload sharing. Notification and
release metadata still use one UDS connection per subscriber.

This phase does not add a subscriber queue, ring buffer, backpressure policy, public loaned sample,
or sample view.

## Pool Lifetime And Ownership

An SHM publisher generates a `/mw_p4_<pid>_<pool-id>` name, computes the complete checked layout,
advertises its descriptor through the registry, and creates the POSIX object with the existing
move-only `SharedMemoryRegion`. Creation performs one `shm_open`, one `ftruncate`, and one writable
`mmap`. The pool remains mapped until publisher destruction.

The publisher owns the SHM name, writable mapping, free lists, chunk state changes, reference-count
changes, reclamation, and final `shm_unlink`. A subscriber receives the name, pool ID, segment size,
layout version, and topic ID through discovery/notification metadata. It opens one read-only,
non-name-owning mapping on its first notification and retains it for its endpoint lifetime.

Normal publisher destruction closes data connections, unmaps, and unlinks the pool. Normal
subscriber destruction unmaps and unsubscribes. An unlink removes the name without invalidating an
already open mapping. Orphan cleanup after `SIGKILL` is deferred to Phase 6.

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

`LOANED` is internal allocator state, not a public loan API. Allocation removes one index from its
publisher-local per-class free list and changes `FREE -> LOANED`. `writeAndPublish()` writes the
payload and metadata once, initializes the subscriber reference count, and publishes the state.
The last valid release changes `PUBLISHED -> RELEASED`; explicit reclaim returns the index to the
same class and changes `RELEASED -> FREE`.

Only the publisher touches free lists. A publisher-local `std::mutex` serializes allocation,
lifecycle validation, reference changes, and reclamation. The retained vector capacities mean
free-list pop/push does not allocate chunk storage or allocate a payload in the hot path. There is
no lock-free free list, per-thread cache, or interprocess mutex.

Invalid transitions, allocation of a non-free chunk, publish outside `LOANED`, reclaim before zero
references, duplicate release, invalid index/offset/pool ID, and stale generation are rejected.

## Multi-Subscriber Sharing And Release

Registry control protocol v3 returns every compatible subscriber endpoint plus the publisher pool
descriptor. The publisher connects one data UDS to each discovered subscriber. One publish does:

```text
allocate one chunk
copy application payload into that chunk once
set ref_count = connected subscriber count
send the same fixed ChunkHandle notification to N sockets
poll N fixed RELEASE frames
decrement exactly once per connection
reclaim at ref_count == 0
```

Subscribers do not decrement the atomic directly. Instead, each successfully decoded notification
can emit exactly one release on its connection after its owning copy finishes. The publisher is the
single refcount writer and tracks one outstanding reference per endpoint, which avoids a
subscriber/free-list synchronization contract and prevents a duplicate frame from decrementing
twice. A normal disconnect while that connection has one known outstanding notification releases
that connection's reference. Abrupt subscriber death and repair of an ambiguous outstanding
reference remain Phase 6 work.

Initial discovery waits before allocating when no subscriber exists, preserving publisher-first
startup without leaving a chunk in `PUBLISHED`. If every notification delivery fails after
allocation, the publisher removes all assigned references and reclaims the chunk.

## Notification Protocol

A pool notification is fixed at 272 bytes: 80 bytes of versioned metadata plus a bounded 192-byte
SHM name field. It carries no business payload. A release is fixed at 40 bytes and includes pool ID,
chunk index, generation, payload offset, and sequence. Both are explicitly big-endian encoded and
validated; C++ structure padding is not sent over UDS.

## Copy And Allocation Semantics

Phase 4 is not zero-copy. One application-buffer-to-SHM copy remains at the publisher. The current
public API returns owning `ReceivedMessage`, so each subscriber also copies the shared payload into
its own `std::vector`. What Phase 4 removes is N separate SHM payload copies and all per-message
`shm_open`/`ftruncate`/`mmap`/`munmap`/`shm_unlink` operations.

The chunk allocator performs no per-message payload allocation. Publisher connection bookkeeping,
notification/release polling containers, control-protocol strings/vectors, and each owning
subscriber message can still allocate ordinary process heap memory. Their measurement and later
optimization are not Phase 4 goals.

## Known Limitations

- No subscriber ring buffer or queue.
- No `DROP_NEWEST`, `DROP_OLDEST`, or blocking backpressure policy.
- No public `LoanedSample` or `SampleView`.
- No `eventfd`, shared queue, or `SCM_RIGHTS` optimization.
- A release timeout can retain a published chunk because safe crash-time reference repair is not
  implemented until Phase 6.
- No heartbeat or dead-process detection.
- No ROS2 adapter and no benchmark conclusion.
