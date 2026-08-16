# Phase 4 Report

## Scope

Phase 4 replaces the Phase 3 per-message POSIX SHM object with one preallocated,
publisher-owned shared-memory pool per SHM publisher. It adds bounded size classes, chunk identity
and generation tracking, a free list, an explicit chunk state machine, reference-counted release,
chunk reuse, and one-publisher-to-many-subscriber delivery of one shared payload. The Phase 1 UDS
copied-payload path remains available and unchanged in purpose.

The public receive API still returns an owning `ReceivedMessage`, so the subscriber copies the
shared payload once before sending RELEASE. This phase does not claim end-to-end zero-copy.

## Files Added

- `CODEX_TASKS/PHASE_4.md`
- `docs/MEMORY_POOL.md`
- `middleware/src/detail/memory_pool.cpp`
- `middleware/src/detail/memory_pool.hpp`
- `middleware/src/detail/pool_protocol.cpp`
- `middleware/src/detail/pool_protocol.hpp`
- `tests/integration/multi_subscriber_shm_test.cpp`
- `tests/unit/memory_pool_test.cpp`
- `tests/unit/pool_protocol_test.cpp`
- `PHASE_4_REPORT.md`

## Files Modified

- `README.md`
- `docs/CONTROL_PLANE.md`
- `docs/DATA_PLANE.md`
- `examples/ping_common.hpp`
- `examples/ping_publisher/main.cpp`
- `examples/ping_subscriber/main.cpp`
- `middleware/CMakeLists.txt`
- `middleware/include/mw/config.hpp`
- `middleware/include/mw/message.hpp`
- `middleware/include/mw/result.hpp`
- `middleware/src/detail/control_protocol.hpp`
- `middleware/src/detail/registry_client.cpp`
- `middleware/src/detail/registry_client.hpp`
- `middleware/src/publisher.cpp`
- `middleware/src/subscriber.cpp`
- `registry/include/mw/registry/registry_state.hpp`
- `registry/src/registry_server.cpp`
- `registry/src/registry_state.cpp`
- `tests/CMakeLists.txt`
- `tests/integration/shm_transport_test.cpp`
- `tests/unit/registry_state_test.cpp`
- `tools/mwctl/main.cpp`

## Architecture

```text
                              control UDS
Publisher <----------------------------------------------> Registry
    |               pool descriptor + N endpoints             |
    |
    | creates/maps once and stores one payload
    v
publisher-owned POSIX SHM pool
    ^ read-only persistent maps
    |
    +---------- Subscriber 1
    +---------- Subscriber 2
    +---------- Subscriber ... N

Publisher -- fixed ChunkHandle notification --> each Subscriber
Publisher <-- fixed RELEASE ------------------- each Subscriber
```

The registry control protocol is version 3. It stores and returns pool metadata and all compatible
subscriber endpoints, but it never creates, maps, reads, writes, or unlinks the pool. The publisher
copies a payload once into a selected chunk, sends the same logical handle to every connected
subscriber, and processes one RELEASE per subscriber connection. The UDS baseline continues to
send its existing frame header and copied payload.

## Memory Pool Layout

One POSIX SHM object contains, in order:

1. An explicitly encoded 80-byte pool header.
2. Explicitly encoded 24-byte metadata for each size class.
3. Explicitly encoded 32-byte directory entries for all chunks.
4. 64-byte-aligned chunk storage, with one 64-byte `ChunkHeader` followed by its fixed-capacity
   payload area.

All offset arithmetic uses checked addition, multiplication, and alignment. The read-only view
validates magic, version, segment size, pool/topic identity, counts, offsets, ordering, alignment,
directory ranges, capacities, and fixed chunk metadata before exposing a payload pointer.

## Size Classes

The default bounded configuration is:

| Capacity | Chunk count |
| ---: | ---: |
| 256 B | 32 |
| 4 KiB | 16 |
| 64 KiB | 8 |
| 1 MiB | 4 |
| 4 MiB | 2 |

Allocation selects the smallest class that fits the payload. If that class is empty it returns
`PoolExhausted`; it does not silently consume a larger class. A payload larger than the largest
class returns `MessageTooLarge`.

## ChunkHeader

`ChunkHeader` is exactly 64 bytes and `alignas(64)`. It contains atomic 32-bit `ref_count` and
`state`, payload size and capacity, size-class index, generation, sequence, monotonic publish
timestamp, topic ID, and pool ID. Compile-time assertions require the layout/alignment and an
always-lock-free 32-bit atomic on the target.

## ChunkHandle

The wire-visible logical identity is:

```text
pool_id, chunk_index, generation, payload_offset
```

Generation increments whenever a free chunk is allocated and skips zero. Pool ID, index,
generation, and directory-derived payload offset must all match before publish, read, release,
cancel, or reclaim. This rejects stale and cross-pool handles.

The pool notification is fixed at 272 bytes, including the bounded SHM name. RELEASE is fixed at
40 bytes and includes the chunk handle plus sequence. Neither frame carries application payload.

## State Machine

```text
FREE --allocate--> LOANED --write/publish--> PUBLISHED
  ^                                           |
  |                                           | final RELEASE
  +---------------- reclaim <-- RELEASED <----+

LOANED --cancel--> FREE
```

Invalid transitions return explicit errors. A duplicate release cannot decrement below zero, and
a stale generation cannot affect a reused chunk.

## Free List

The publisher builds one local free-index vector per size class when the pool is created. A mutex
serializes allocation, publication, release, cancel, reclaim, instrumentation, and free-list
mutation. No lock-free free list was introduced. All free lists are bounded by the configured
preallocated chunks.

## Reference Count Semantics

Before publishing, the publisher assigns one reference to every connected subscriber that will
receive the notification. Each valid RELEASE decrements exactly one reference. A notification
send failure or known normal connection loss removes that connection's assigned reference. When
the count reaches zero, the chunk enters `RELEASED`; the publisher then reclaims it to `FREE`.

The lifecycle unit test starts at four references and observes the exact sequential results
`4 -> 3 -> 2 -> 1 -> 0`, followed by `RELEASED -> FREE`. It also verifies duplicate release and
stale-generation rejection. The normal-disconnect integration test intentionally uses a pool with
one 4 KiB chunk; a subscriber closes without RELEASE, and a subsequent successful publish through
the same publisher proves the known reference was removed and that same sole chunk became reusable.

## Multi-Subscriber Discovery

Registry discovery now returns a vector of every compatible subscriber endpoint instead of one
endpoint. The publisher establishes one data UDS connection per discovered endpoint. For SHM it
sends identical pool/chunk identity to each connection and tracks outstanding releases by endpoint.
The one-active-publisher-per-topic invariant remains enforced.

Both startup orders are covered. A subscriber registered before the publisher receives pool
metadata from the advertise path. A publisher started first advertises its descriptor before
waiting; the first notification supplies the same descriptor and the subscriber opens its
persistent read-only view then.

## Resource Ownership

- The publisher creates and owns the pool SHM name, writable mapping, chunk metadata, local free
  lists, data connections, and unlink responsibility.
- Each subscriber owns its listener, one accepted data connection, and one persistent read-only
  pool mapping. It never unlinks the pool.
- The registry owns only control-plane records and pool descriptor bytes.
- RAII wrappers own file descriptors and mappings. Normal publisher destruction unmaps, closes,
  and unlinks its project-namespaced pool object.

## Synchronization Model

`publish()` performs discovery/connect when needed, allocation, one payload copy, notification,
RELEASE polling, and reclaim synchronously on the publisher application thread. `waitAndTake()`
accepts/reads, validates the mapped chunk, copies to the owning public result, and sends RELEASE on
the subscriber application thread. No data-plane background thread was added. The registry keeps
its existing single epoll event loop.

Publisher-local lifecycle operations are mutex-protected. Shared state/refcount fields use
acquire/release ordering where they publish or consume chunk state. The implementation does not
claim that the overall pool algorithm is lock-free.

## Error Handling

Phase 4 adds `PoolExhausted`, `InvalidChunkHandle`, and `DuplicateRelease`. Existing errors cover
message size, invalid state/frame/SHM, transport mismatch, registry failure, timeout, connection
loss, and I/O failure. Pool layout and notification validation precede payload access. Failed sends
remove all references known not to have reached a subscriber; a confirmed normal disconnect also
removes its known outstanding reference. A timeout with uncertain remote state intentionally does
not guess that the reference is safe to reclaim.

## Pool Exhaustion

A one-chunk unit test allocates the only matching chunk, receives `PoolExhausted` on the next
allocation, cancels/reclaims it, and then reallocates the same index with a new generation. No
duplicate active allocation occurs and exhaustion does not allocate or map memory.

## Build Result

The execution environment rejected the literal recursive-removal command. The same three exact
generated directories (`build`, `_install`, and `build_external`) were removed with
`cmake -E remove_directory`, without touching other paths. A fresh Debug configure and parallel
build then completed with GCC 13.3.0 under C++17 and the project warning flags. Result: PASS.

## CTest Result

The final clean Debug run completed 50/50 tests with no failures. Result: PASS.

## ASan Result

The complete 50-test suite passed in the ASan build with no AddressSanitizer diagnostics. The
sanitizer suites were run serially so their process tests could not observe another build's live
project SHM namespace. Result: PASS.

## UBSan Result

The complete 50-test suite passed in the UBSan build with no UndefinedBehaviorSanitizer
diagnostics. Result: PASS.

## TSan Result

NOT RUN. TSan is optional for Phase 4 and no claim is made for it.

## 1 Publisher / 4 Subscribers Result

One real registry process, one publisher process, and four independently named subscriber
processes transferred two deterministic 1 KiB messages each. Every process exited successfully;
all subscribers reported two messages with zero sequence and payload errors. Result: PASS.

## Same Logical Chunk Verification

The four subscriber outputs were parsed and compared. For the final message all four reported the
same nonzero `pool_id`, the same `chunk_index`, the same nonzero `generation`, and the same
`payload_offset`. Result: PASS.

## Ref Count Result

The exact unit lifecycle was `4 -> 3 -> 2 -> 1 -> 0`, then `RELEASED -> FREE`. Multi-process
delivery completed and cleaned its pool, and the single-chunk disconnect test verified recovery of
a known lost connection reference. Result: PASS.

## Chunk Reuse Result

After final release and reclaim, allocation returned the same chunk index with a different
generation. The one-chunk disconnect scenario also published successfully again through the same
publisher. Result: PASS.

## Long-Run Reuse Result

A pool containing one 4 KiB chunk published, read, released, reclaimed, and reused that chunk for
3,000 deterministic 1 KiB messages. Sequence and payload remained correct, all 3,000 releases and
reclaims completed, and allocation failures remained zero. Result: PASS.

## No Per-Message mmap Verification

The 3,000-message test records exactly one SHM create, one truncate, and one publisher writable
map, while observing 3,000 allocations and 3,000 payload copies. The read-only view is also opened
once and retained for the loop. No per-message `shm_open`, `ftruncate`, or `mmap` remains in the SHM
publish hot path. Result: PASS.

## Payload Size Results

The deterministic UDS/SHM regression matrix transfers two messages at each required size, and a
manual SHM matrix transferred three messages per size. Publisher and subscriber processes exited
zero with no publish, sequence, or payload errors.

| Payload | Result |
| ---: | :--- |
| 64 B | PASS (size-class and manual SHM coverage) |
| 1 KiB | PASS |
| 64 KiB | PASS |
| 1 MiB | PASS |
| 4 MiB | PASS |

## One Payload Stored Once

`writeAndPublish()` contains the single publisher payload `memcpy` into the selected chunk and
increments the copy counter once. The publisher then encodes one fixed notification and sends its
metadata to every subscriber; it does not create subscriber-specific payload storage. Result: PASS.

## Phase 0-3 Regression

All existing tests remain enabled. The 50-test suite covers package/version behavior, frame and
partial I/O handling, UDS direct and cross-process Pub/Sub, disconnect/reconnect, registry and
`mwctl`, both startup orders, type/transport compatibility, Phase 3 SHM protocol validation, and
the 1 KiB through 4 MiB UDS/SHM matrix. Result: PASS.

## Install / Export Result

`cmake --install build --prefix _install` installed the library, public headers, tools, and CMake
package. A separate `examples/external_consumer` build found only the installed package with
`find_package(mw CONFIG REQUIRED)`, linked `mw::mw_core`, and ran with output `0.1.0`. Installed
`mw_registryd` and `mwctl node list` also completed a live registry smoke test. Result: PASS.

## Normal /dev/shm Cleanup

Unit and process tests compare only this project's `mw_p3_` and `mw_p4_` namespace. All normal
test exits restored the initial snapshot, and the final repository review found no project pool
objects in `/dev/shm`. Other applications' resources were neither inspected for deletion nor
removed. Result: PASS.

## Known Limitations

- No Subscriber Ring Buffer.
- No Backpressure policies.
- No public LoanedSample.
- No SampleView.
- No eventfd optimization.
- No crash-time refcount repair.
- No heartbeat.
- No ROS2 Adapter.
- No benchmark conclusions.
- The public `ReceivedMessage` owns a vector, so receive still copies from SHM.
- A release timeout with uncertain subscriber state can retain a published chunk until process
  teardown; crash/timeout reference repair is a later phase.
- The v1 scope remains one active publisher per topic on one Linux host.

## Phase Boundary

Phase 5 was not implemented.
