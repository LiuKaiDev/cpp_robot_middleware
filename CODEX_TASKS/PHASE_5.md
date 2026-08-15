# Codex Task - Phase 5: Ring Buffer, Backpressure, LoanedSample, And SampleView

## Scope

Implement Phase 5 only: add one bounded shared-memory subscriber queue per SHM endpoint, explicit
overflow policies, publisher-side payload loans, and read-only subscriber views. Preserve the UDS
baseline, the normal copied SHM API, one active publisher per topic, and N subscribers.

## Required Architecture

- Store only generation-protected `ChunkHandle` values in a fixed-capacity ring buffer owned by
  each subscriber and mapped read-write by its publisher.
- Use process-shared pthread mutex/condition primitives with monotonic timed waits. Do not add a
  lock-free queue or robust owner-death repair.
- Implement per-subscriber `DROP_NEWEST`, `DROP_OLDEST`, and `BLOCK_WITH_TIMEOUT` behavior with
  exact reference release for rejected or displaced handles.
- Prevent the enqueue/reclaim race with a publisher guard reference until all endpoint decisions
  have completed.
- Keep UDS as a fixed metadata wake/release channel; the queue is the source of truth and wakes may
  be coalesced.
- Add `Publisher::loan()`, move-only RAII `LoanedSample`, cancellation of unpublished loans,
  double-publish protection, and an explicit unsupported result on UDS.
- Add move-only, read-only RAII `SampleView` and `takeView()`/`waitAndTakeView()`. A view owns a
  shared pool mapping and release context so it can safely outlive its `Subscriber` object.
- Keep `publish()` as the SHM copy path and `take()`/`waitAndTake()` as compatible owning APIs.

## Verification

- Test empty/full queues, FIFO order, wraparound, thousands of cycles, all policies, monotonic
  block timeout, and successful producer wake.
- Test loan size/capacity, pool exhaustion, cancellation, move construction/assignment, writable
  payload, publish, double publish, UDS rejection, and reuse.
- Test read-only views, metadata, move/release behavior, lifetime beyond `Subscriber`, chunk hold,
  and reuse after final release.
- Verify one loaned logical chunk reaches four subscribers with matching pool ID, chunk index,
  generation, offset, sequence, and payload.
- Verify 1 KiB, 64 KiB, 1 MiB, and 4 MiB loaned messages, the retained SHM copy path, mixed queue
  behavior, pool-exhaustion separation, and normal pool/queue namespace cleanup.
- Retain all Phase 0-4 tests and run clean Debug, ASan, UBSan, install/export, and external-consumer
  acceptance.
- Add `docs/QUEUES_AND_LOANING.md`, update relevant docs, and produce `PHASE_5_REPORT.md`.

## Deferred Work

Do not implement heartbeat, endpoint timeout/dead state, SIGKILL recovery, crash-time queue or
refcount repair, stale resource scavenging, ROS2, benchmarks, full metrics, or Phase 6 work.
`eventfd` is optional and may remain deferred after mandatory correctness is complete.
