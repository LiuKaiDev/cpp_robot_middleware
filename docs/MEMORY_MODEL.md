# Memory Model

## Shared Memory Ownership

An SHM publisher owns one preallocated POSIX shared-memory pool for its endpoint lifetime. It owns
the name, writable mapping, free lists, chunk state, refcounts, and normal unlink. Subscribers map
that pool read-only. Each subscriber separately owns one read-write bounded queue; the publisher
maps the queue but does not own its name.

`SharedMemoryRegion` is the move-only RAII wrapper for fd, mapping, unmap, close, and optional name
ownership. There is no per-message `shm_open`, `ftruncate`, `mmap`, or `munmap`.

## Pool Layout

```text
+--------------------------------+
| encoded PoolHeader (80 B)      |
+--------------------------------+
| SizeClassMetadata[] (24 B each)|
+--------------------------------+
| ChunkDirectoryEntry[] (32 B)   |
+--------------------------------+ 64-byte aligned
| ChunkHeader (64 B) + payload   |
| ...                            |
+--------------------------------+
```

The encoded header records magic/version, exact segment size, pool/topic IDs, owner PID, counts,
and offsets. Directory entries describe header/payload offsets, capacity, and class. Checked
arithmetic and overlap/alignment validation run before payload access.

## Size Classes And Free Lists

Default classes are 256 B x32, 4 KiB x16, 64 KiB x8, 1 MiB x4, and 4 MiB x2. Allocation chooses
the smallest fitting class. Exhaustion returns `PoolExhausted`; it does not borrow from a larger
class or allocate an unbounded payload.

Only the publisher uses the per-class free-list vectors. A publisher-local mutex protects
allocation, lifecycle transitions, refcount changes, and reclamation. The implementation is
intentionally not a lock-free allocator.

## Chunk Header

Each aligned native `ChunkHeader` contains:

- atomic lifecycle state and atomic `ref_count`;
- payload size and capacity;
- size-class index and allocation generation;
- sequence and monotonic publish timestamp;
- topic ID and pool ID.

The supported Linux/compiler configuration requires native 32-bit atomics to be always lock-free.
This shared native header is a local-host ABI, not a portable file or network format.

## Logical Chunk Identity

`ChunkHandle` is value-only:

```text
(pool_id, chunk_index, generation, payload_offset)
```

Processes may map the same object at different virtual addresses, so pointers must not be compared
across processes. Generation increments on allocation and protects a new use of an index from an
old delayed release.

## Reference Ownership

The publisher is the only refcount writer. Publication starts with one guard reference, adds one
tentative reference before each queue enqueue, and releases the guard after all endpoint decisions.
Accepted queue entries transfer the tentative reference to that endpoint. Drops, timeouts, failed
wakes, or displaced `DROP_OLDEST` handles return the corresponding reference.

Each publisher connection tracks a bounded vector of outstanding handles. A `SampleView`
destructor sends one release frame. Disconnect or dead-subscriber reconciliation drains any
remaining obligations exactly once.

## SHM Copy Path

```text
application buffer
  -> one memcpy into an allocated pool chunk
  -> N queues receive the same handle
  -> SampleView reads mapped bytes
```

Calling the owning subscriber API adds one mapped-memory-to-vector copy.

## SHM Loan Path

```text
Publisher::loan(size)
  -> application fills LoanedSample::data() in the pool
  -> publish the same logical chunk
  -> SampleView reads that chunk
```

This native loan-to-view path was verified to avoid middleware payload copies. The ROS2 adapter
still serializes into an adapter buffer and copies serialized bytes into the loan.

## Cleanup

Normal owners close/unmap/unlink through RAII. On publisher death, the registry unlinks the exact
pool name and surviving subscribers reset only that pool after existing local views finish. On
subscriber death, the registry robust-closes/unlinks its exact queue and the live publisher repairs
outstanding references. Arbitrary memory corruption and registry-daemon restart are outside the
recovery model.

See [MEMORY_POOL.md](MEMORY_POOL.md) for field-level validation and
[MESSAGE_LIFECYCLE.md](MESSAGE_LIFECYCLE.md) for transitions.
