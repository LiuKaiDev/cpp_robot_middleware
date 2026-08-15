# Codex Task - Phase 4: Memory Pool, Chunk Lifecycle, And Multi-Subscriber

## Scope

Implement Phase 4 only: replace per-message SHM objects with one publisher-lifetime preallocated
pool, implement bounded size classes and reusable chunk lifecycle, and share one logical payload
from one active publisher with N subscribers while retaining the owning public receive API.

## Required Architecture

- Reuse the Phase 3 move-only `SharedMemoryRegion`; create/map one pool at publisher startup and
  map it once per subscriber.
- Provide configurable 256 B, 4 KiB, 64 KiB, 1 MiB, and 4 MiB size classes with a mutex-protected,
  publisher-owned free list.
- Implement checked pool/header/class/directory/chunk layout, aligned lock-free atomic uint32 state
  and refcount fields, generation-protected `ChunkHandle`, and
  `FREE -> LOANED -> PUBLISHED -> RELEASED -> FREE`.
- Copy one payload into one chunk, send the same handle over one UDS connection per discovered
  subscriber, receive exactly one release per delivered handle, and reclaim only at zero refs.
- Extend registry protocol/discovery with all compatible subscriber endpoints and pool name, ID,
  size, and layout version. Registry never maps or owns the pool.
- Return immediate `PoolExhausted` for a busy selected class and retain `MessageTooLarge` for an
  unavailable class.
- Preserve the UDS copied baseline, direct mode, one-active-publisher rule, synchronous application
  thread model, install/export contract, and owning `ReceivedMessage` compatibility.

## Verification

- Unit-test every default size class, 1000 B selection, exhaustion, no duplicate active allocation,
  state/refcount transitions, duplicate/stale release, generation reuse, corrupt layout rejection,
  normal unlink, and thousands of cycles through an intentionally one-chunk pool.
- Verify instrumentation remains one create/truncate/writable-map operation after pool startup.
- Run a real registry, one SHM publisher, and four subscriber processes; require equal sequence,
  payload, pool ID, chunk index, generation, and payload offset.
- Retain all Phase 0-3 tests, the 1 KiB/64 KiB/1 MiB/4 MiB matrix, normal namespace cleanup,
  Debug/ASan/UBSan builds, install/export, and external consumer acceptance.
- Add `docs/MEMORY_POOL.md`, update README/control/data-plane docs, and produce
  `PHASE_4_REPORT.md` with measured results.

## Prohibited Phase 5+ Work

Do not implement subscriber ring buffers/queues, backpressure or drop/block policies, public
`publisher.loan()`, `LoanedSample`, `SampleView`, `eventfd`, `SCM_RIGHTS`, lock-free free lists,
heartbeat/crash repair, ROS2, benchmark infrastructure, or performance claims. Do not call the
Phase 4 path zero-copy.
