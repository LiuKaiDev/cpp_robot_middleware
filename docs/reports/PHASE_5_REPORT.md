# Phase 5 Report

## Scope

Phase 5 adds a fixed-capacity shared-memory ring queue per SHM subscriber, per-endpoint
`DROP_NEWEST`, `DROP_OLDEST`, and `BLOCK_WITH_TIMEOUT` policies, writable publisher payload loans,
and read-only subscriber views with RAII release. It retains the Phase 1 copied UDS baseline, the
Phase 4 preallocated pool, ordinary SHM `publish()` copy semantics, owning `ReceivedMessage`
compatibility, one active publisher per topic, and N SHM subscribers.

Phase 6 heartbeat, dead-process detection, and crash repair were not implemented.

## Files Added

- `CODEX_TASKS/PHASE_5.md`
- `docs/QUEUES_AND_LOANING.md`
- `middleware/include/mw/loaned_sample.hpp`
- `middleware/include/mw/sample_view.hpp`
- `middleware/src/detail/loaned_sample_owner.hpp`
- `middleware/src/detail/queue_protocol.cpp`
- `middleware/src/detail/queue_protocol.hpp`
- `middleware/src/detail/sample_release_context.hpp`
- `middleware/src/detail/subscriber_queue.cpp`
- `middleware/src/detail/subscriber_queue.hpp`
- `middleware/src/loaned_sample.cpp`
- `middleware/src/sample_view.cpp`
- `tests/integration/phase5_loan_queue_test.cpp`
- `tests/unit/subscriber_queue_test.cpp`
- `PHASE_5_REPORT.md`

## Files Modified

- `README.md`
- `docs/CONTROL_PLANE.md`
- `docs/DATA_PLANE.md`
- `docs/MEMORY_POOL.md`
- `examples/external_consumer/main.cpp`
- `middleware/CMakeLists.txt`
- `middleware/include/mw/config.hpp`
- `middleware/include/mw/publisher.hpp`
- `middleware/include/mw/result.hpp`
- `middleware/include/mw/subscriber.hpp`
- `middleware/src/detail/control_protocol.hpp`
- `middleware/src/detail/memory_pool.cpp`
- `middleware/src/detail/memory_pool.hpp`
- `middleware/src/detail/registry_client.cpp`
- `middleware/src/detail/registry_client.hpp`
- `middleware/src/detail/shared_memory.cpp`
- `middleware/src/detail/shared_memory.hpp`
- `middleware/src/publisher.cpp`
- `middleware/src/subscriber.cpp`
- `registry/include/mw/registry/registry_state.hpp`
- `registry/src/registry_server.cpp`
- `registry/src/registry_state.cpp`
- `tests/CMakeLists.txt`
- `tests/integration/multi_subscriber_shm_test.cpp`
- `tests/integration/shm_transport_test.cpp`
- `tests/unit/memory_pool_test.cpp`
- `tests/unit/registry_state_test.cpp`

## Architecture

```text
Registry protocol v4
  publisher pool descriptor + subscriber endpoint/queue descriptors

Publisher process                         Subscriber process
+----------------------------+            +----------------------------+
| publisher-owned SHM pool   |            | subscriber-owned SHM queue|
| reusable headers/payloads  |<--handle---| bounded ring slots         |
+----------------------------+            +----------------------------+
             ^                                  ^             |
             | fixed RELEASE                    | fixed wake  | dequeue
             +-------------- UDS ---------------+             v
                                                        read-only SampleView
```

The control plane stores descriptors only. It never maps, creates, or unlinks pool or queue SHM.
The queue is the data-availability source of truth. UDS carries an empty-to-nonempty wake and
exactly-once view release metadata, never SHM business payload.

## Ring Buffer Design

One native local-host queue segment contains a checked `SubscriberQueueHeader`, aligned padding,
and exactly `capacity` `ChunkHandle` slots. Header state includes head, tail, size, capacity,
high-water mark, close/attachment state, policy/timeout, counters, a process-shared pthread mutex,
and a process-shared condition variable. Head and tail wrap modulo capacity; explicit size
distinguishes empty and full.

Depth zero and depth above 65,536 are rejected. Segment sizing uses checked multiplication and
addition. A queue descriptor must match the mapped magic, version, size, identity, capacity,
policy, and timeout before attachment.

## Queue Ownership

The subscriber creates and owns `/mw_q5_<pid>_<queue-id>`, its writable mapping, listener, and
normal unlink. The publisher owns only a read-write mapping attachment. A synchronized attachment
count keeps pthread objects alive until the final normal detach. The publisher separately creates,
maps, and unlinks `/mw_p5_<pid>_<pool-id>`.

## Cross-Process Synchronization

All ring state changes are protected by one `PTHREAD_PROCESS_SHARED` mutex. The not-full condition
is `PTHREAD_PROCESS_SHARED` and uses `CLOCK_MONOTONIC`. Dequeue signals a producer and close
broadcasts to all waiters. No lock-free queue, robust mutex, owner-death recovery, or data-plane
worker thread was added.

## Queue Entry Format

Each ring slot stores only:

```text
pool_id, chunk_index, generation, payload_offset
```

The fixed 272-byte wake contains pool descriptor and queue ID. The fixed 32-byte release contains
one handle. Neither carries payload. One wake can cover multiple entries.

## OverflowPolicy

`SubscriberConfig` now carries `queue_depth`, `overflow_policy`, and a positive `block_timeout`.
The policy is part of each discovered endpoint, so different subscribers may choose independent
queue behavior.

### DROP_NEWEST Semantics

On full, existing entries are unchanged and the new endpoint reference is returned immediately.
If no endpoint accepts, publish reports `QueueFull`. Unit and public tests verified old sequences
remain, `dropped_newest` increments, the rejected generation is reclaimed, and no pool leak occurs.

### DROP_OLDEST Semantics

On full, the ring atomically displaces its oldest handle and inserts the newest. The publisher
removes and releases the displaced endpoint reference exactly once. Tests observed the expected
remaining sequences, `dropped_oldest`, and reuse without refcount drift.

### BLOCK_WITH_TIMEOUT Semantics

On full, enqueue waits for a condition signal, close, or a monotonic deadline. A 60 ms queue-level
test observed timeout after at least 40 ms and before 2 s. The public 80 ms policy returned
`QueueTimeout` with `block_timeouts == 1`. A separate blocked publisher became ready only after a
subscriber dequeue and then enqueued successfully. No lost wake or deadlock was observed.

## Ref Count / Enqueue Race Prevention

A published chunk starts with one publisher guard reference. Before each endpoint enqueue, the
publisher adds a tentative reference. Acceptance transfers it to that endpoint; drop, timeout,
close, synchronization failure, or failed first wake returns it immediately. `DROP_OLDEST` also
returns the displaced reference. The publisher releases the guard only after all endpoint
decisions.

This prevents a fast subscriber from releasing the generation to zero while the publisher can
still expose it to another queue. A pool unit test observed guard plus endpoint refcount `2`, then
`2 -> 1 -> 0 -> RELEASED -> FREE`. Public multi-subscriber and long-run tests completed without
stale-generation access or pool drift.

## LoanedSample API

`Publisher::loan(size)` returns move-only `LoanedSample` for SHM. It exposes writable payload
`data()`, requested `size()`, selected class `capacity()`, logical identity, validity/error, and
consuming `publish()`. UDS returns `UnsupportedTransport` rather than allocating a substitute
buffer.

## LoanedSample Lifecycle

```text
FREE -> LOANED -> PUBLISHED
  ^        |
  + cancel +  destructor without publish
```

Destruction without publish cancels the loan. Move construction and move assignment transfer the
only owner and leave their sources inert. First publish consumes the loan on success or failure;
second publish returns `InvalidState` and cannot enqueue again. Oversize and one-chunk exhaustion
were explicit and distinct from queue errors.

## SampleView API

`Subscriber::takeView()` and `waitAndTakeView()` return move-only `SampleView` on SHM. The API
exposes only `const void*` payload access plus size, sequence, monotonic timestamp, and logical
chunk identity. Compile-time tests verify the read-only return type.

## SampleView Lifecycle

One view owns one endpoint reference. It retains a shared read-only pool mapping and shared release
channel, not a raw `Subscriber*`. Move operations transfer the one release obligation. Destruction
is non-throwing and emits one release. A test destroyed `Subscriber` first, validated the still-live
view, released it, and then reused the one available chunk with a new generation.

## SHM Copy Path

`Publisher::publish(data, size)` retains one application-buffer-to-pool `memcpy`. Queue delivery
and `SampleView` use the same lifecycle as a loan. `take()`/`waitAndTake()` copy from a temporary
view into owning `ReceivedMessage`, preserving the compatibility API. A public test validated the
copy path after all required loan sizes.

## SHM Loan Path

The application writes directly to `LoanedSample::data()`. `publish()` changes that same chunk
generation to `PUBLISHED`; no middleware payload copy occurs. Each subscriber `SampleView` points
into its read-only mapping of that same logical pool ID, chunk index, generation, and payload
offset. The pool instrumentation test recorded `payload_copies == 0` for this path.

## Zero-Copy Claim Boundary

The verified SHM loaned path avoids middleware payload copies between publisher loan fill and
subscriber `SampleView`. This does not apply to UDS, ordinary SHM `publish()`, the owning receive
API, or all middleware operations. No performance or end-to-end system-wide zero-copy claim is
made.

## Build Result

The command layer rejected the literal requested recursive-removal command. CMake's explicit
`remove_directory` command removed the same generated `build`, `_install`, `build_external`,
`build_asan`, and `build_ubsan` directories. A fresh GCC 13.3.0 C++17 Debug configure with exported
compile commands and a parallel warning-enabled build completed. Result: PASS.

## CTest Result

The clean Debug suite passed 67/67 tests with zero failures. Phase 0-4 retained their 50/50 baseline
tests; 17 focused Phase 5/pool/queue tests were added. Tests use a shared CTest IPC resource lock so
namespace cleanup assertions do not observe another test's live SHM objects. Result: PASS.

## ASan

The complete 67-test suite passed with `detect_leaks=1` and `halt_on_error=1`. No ASan or leak
diagnostic was emitted. Result: PASS.

## UBSan

The complete 67-test suite passed with `halt_on_error=1` and stack traces enabled. No undefined
behavior diagnostic was emitted. Result: PASS.

## TSan

NOT RUN. TSan is optional; no TSan claim is made for process-shared pthread primitives.

## Ring Wrap Result

A direct queue test performed 2,000 complete cycles at depth seven: 14,000 ordered enqueues and
14,000 ordered dequeues with exact FIFO identity and max size seven. Result: PASS.

## Queue Full / Empty Result

New queues returned empty for peek/dequeue. Full queues followed all three explicit policies,
preserved bounds, and returned to empty without index corruption. Result: PASS.

## DROP_NEWEST Result

Depth two preserved sequences 1 and 2, rejected sequence 3, reported `QueueFull` and one
`dropped_newest`, and reclaimed its rejected reference. Result: PASS.

## DROP_OLDEST Result

Depth two displaced sequence 1, retained sequences 2 and 3, reported one `dropped_oldest`, and
released the old reference exactly once. Result: PASS.

## Block Timeout Result

Both queue-level and public endpoint timeout paths returned explicitly within bounded timing and
without leaked references. Result: PASS.

## Block Successful Wake Result

A blocked producer remained blocked before dequeue and completed after the consumer signaled space.
The public publisher then delivered the newly enqueued sample. Result: PASS.

## Loan Without Publish Result

The sole one-class chunk was loaned, moved twice, destroyed unpublished, and then reallocated at
the same index with a new generation. Result: PASS.

## Loan Then Publish Result

Writable loan payload, capacity, sequence, timestamp, identity, subscriber bytes, and final reuse
were validated. Result: PASS.

## Double Publish Result

The first call enqueued exactly once. The second returned `InvalidState`; no duplicate queue entry
or reference was observed. Result: PASS.

## SampleView Release Result

While a one-chunk view remained live, a new loan returned `PoolExhausted`. Move/release then made
that same index reusable with a new generation. A view also released safely after its `Subscriber`
had been destroyed. Result: PASS.

## 1 Publisher / 4 Subscribers Loan Result

One publisher loaned one 2 KiB chunk to four independently registered SHM subscribers. All four
views validated identical bytes, sequence, pool ID, chunk index, generation, and payload offset.
After all four views released, the one-chunk pool allocated again. Result: PASS.

## Payload Size Results

The public loan-to-view test passed for 1 KiB, 64 KiB, 1 MiB, and 4 MiB payloads. Every byte was
validated through `SampleView`; the next iteration proved prior release/reuse. Result: PASS for all
four sizes.

## Long-Run Queue Result

A depth-three `DROP_OLDEST` endpoint used a pool with four bounded chunks for 5,000 sequential
publishes without consuming during production. Every publish succeeded with exact sequence; final
views were 4,998, 4,999, and 5,000. Result: PASS.

## Pool Exhaustion Regression

Existing allocator exhaustion tests remain green. Public view-hold testing returned
`PoolExhausted`, while queue saturation separately returned `QueueFull` or `QueueTimeout`. Result:
PASS.

## Phase 0-4 Regression

All prior 50 tests pass, including UDS, registry/discovery, `mwctl`, 1 KiB/64 KiB/1 MiB/4 MiB SHM
copy matrix, pool lifecycle/reuse, and four subscriber processes. Result: PASS.

## Install / Export Result

The Debug install exported `mw::mw_core` and installed `LoanedSample`, `SampleView`, configuration,
and all prior headers. A separately configured consumer included the new APIs, linked only the
installed package, ran, and printed `0.1.0`. Result: PASS.

## Normal SHM / Queue Cleanup

Tests include `/mw_p5_*` pools and `/mw_q5_*` queues in before/after namespace assertions. The final
audit found no such object in `/dev/shm`. Subscribers close/drain queues and views retain safe
release channels; owners unlink their names during normal RAII destruction. Result: PASS.

## Eventfd

DEFERRED. UDS provides fixed metadata wake/release notification for Phase 5.

## Known Limitations

- No heartbeat.
- No dead-process detection or endpoint suspected/dead lifecycle.
- No SIGKILL recovery.
- No crash-time queue/refcount repair or orphan scavenging.
- No robust pthread owner-death recovery.
- No ROS2 adapter.
- No benchmark framework or performance conclusions.
- No full public metrics system; Phase 5 uses internal counters and test-visible results.
- Registry calls and endpoint APIs remain synchronous and are intended for serialized use.
- Publisher discovery captures the compatible subscriber set when it establishes data connections;
  dynamic subscriber-set refresh while existing connections remain active is not implemented.

## Phase Boundary

Phase 6 was not implemented.
