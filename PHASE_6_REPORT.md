# Phase 6 Report

## Scope

Phase 6 adds Linux-local heartbeat/liveness, node session identity, immediate control-connection
death detection, exact registry/resource cleanup, bounded dead-subscriber reference recovery,
robust process-shared queue repair, and publisher/subscriber replacement after crashes. It preserves
the Phase 1 copied UDS baseline and the Phase 3-5 SHM copy, pool, multi-subscriber, bounded queue,
backpressure, `LoanedSample`, and `SampleView` paths.

Phase 7 ROS2 adapter work was not implemented.

## Files Added

- `CODEX_TASKS/PHASE_6.md`
- `docs/FAILURE_MODEL.md`
- `tests/integration/phase6_crash_recovery_test.cpp`
- `tests/unit/registry_liveness_test.cpp`
- `PHASE_6_REPORT.md`

## Files Modified

- `README.md`
- `docs/CONTROL_PLANE.md`
- `docs/DATA_PLANE.md`
- `docs/MEMORY_POOL.md`
- `docs/QUEUES_AND_LOANING.md`
- `docs/UDS_BASELINE.md`
- `middleware/include/mw/config.hpp`
- `middleware/src/detail/control_protocol.cpp`
- `middleware/src/detail/control_protocol.hpp`
- `middleware/src/detail/queue_protocol.hpp`
- `middleware/src/detail/registry_client.cpp`
- `middleware/src/detail/registry_client.hpp`
- `middleware/src/detail/shared_memory.cpp`
- `middleware/src/detail/shared_memory.hpp`
- `middleware/src/detail/subscriber_queue.cpp`
- `middleware/src/detail/subscriber_queue.hpp`
- `middleware/src/publisher.cpp`
- `middleware/src/subscriber.cpp`
- `registry/include/mw/registry/registry_server.hpp`
- `registry/include/mw/registry/registry_state.hpp`
- `registry/src/main.cpp`
- `registry/src/registry_server.cpp`
- `registry/src/registry_state.cpp`
- `tests/CMakeLists.txt`
- `tests/integration/multi_subscriber_shm_test.cpp`
- `tests/unit/registry_state_test.cpp`
- `tests/unit/subscriber_queue_test.cpp`
- `tools/mwctl/main.cpp`

## Failure Model

The supported failure model is one Linux host with a running registry and middleware processes that
exit normally, close/crash their control connection, stop heartbeating while the socket remains
open, or are killed with `SIGKILL`. The registry cleans only exact resources recorded during
registration. It does not scan namespaces, infer identity from PID, recover a failed host/kernel,
repair arbitrary shared-memory corruption, or provide distributed fault tolerance.

## Heartbeat Architecture

Control protocol version 5 adds `ATTACH_HEARTBEAT` and `HEARTBEAT`. Registration returns a unique
monotonic `node_id` plus `session_id`. Each `RegistrySession` retains the primary synchronous
client, opens a dedicated heartbeat client, authenticates it with both IDs, and owns one RAII
thread. This avoids an outstanding discovery call preventing heartbeat renewal.

Heartbeat responses carry current liveness and bounded peer-death records. The heartbeat thread
only caches metadata. Publisher/subscriber caller threads consume events and mutate data-plane
state, avoiding cross-thread pool, queue, connection, or reference-count changes.

## Heartbeat Timing And Liveness State

Defaults are 250 ms interval, 750 ms suspect timeout, and 1500 ms dead timeout. Both public
`LivenessConfig` and registry CLI options require positive interval < suspect < dead values.
`RegistryState` accepts injectable `steady_clock::time_point` values for deterministic tests.

The state machine is ALIVE -> SUSPECTED -> terminal DEAD. A valid heartbeat in SUSPECTED returns
the record to ALIVE. DEAD cleanup removes the record; that session cannot be revived. `mwctl node
list` reports ALIVE/SUSPECTED, while DEAD nodes are absent.

## Registry Detection And Control Failure

The single-threaded `epoll` registry evaluates monotonic liveness on bounded wait intervals.
Primary control EOF/HUP runs immediate dead-node cleanup rather than waiting for the lease. A
heartbeat connection close detaches that channel; an open primary socket without further renewal
progresses through SUSPECTED and DEAD. Wrong connection, node ID, or session ID heartbeat attempts
are rejected.

## Node And Endpoint Cleanup

Before removal, `RegistryState` constructs one `DeadNodeCleanup` plan containing exact pool, queue,
socket, endpoint, session, and peer-event identities. It then removes node/endpoint/topic records.
The server closes the companion connection, cancels invalid pending discovery, performs registered
resource cleanup, and queues bounded events for surviving peers. Duplicate cleanup accepts missing
records, already-closed descriptors, and already-unlinked names.

Duplicate node-name and one-active-publisher checks remain enforced while records are live. New
registrations are accepted after dead cleanup removes the prior record.

## Resource Ownership

The publisher owns its pool name, mapping, chunk state, free lists, and refcounts. Each subscriber
owns its queue name/mapping and listener path. Peers own non-name-owning mappings. The registry owns
control state and cleanup metadata; after owner death it may robust-open/close a registered queue
and unlink registered names. It never forwards business payloads or scans `/dev/shm`.

Normal exit remains owner-led RAII cleanup plus explicit endpoint/node unregistration. Crash exit is
registry-led namespace cleanup plus surviving peer repair. Both paths are idempotent.

## Publisher Crash Cleanup

The registry unlinks a dead publisher's exact advertised pool and sends `PublisherDead` with the old
pool ID. Each surviving subscriber discards only old-pool queue entries, resets the old release
connection/mapping, and accepts a replacement publisher. Unlink does not invalidate an already-open
mapping or `SampleView`; its local shared owner keeps that mapping valid.

A new-pool handle can reach the subscriber queue before old-socket HUP. The subscriber leaves the
mismatched handle queued, processes the disconnect, then validates the replacement wake and pool
descriptor before reading it. `ECONNRESET`/`ENOTCONN` are treated as recoverable peer loss.

## Subscriber Crash And Reference Recovery

Every publisher connection tracks a vector of handles whose subscriber release is outstanding.
The maximum is the configured finite pool chunk count. A valid release removes one matching entry.
Drop, timeout, and failed-wake paths return tentative references immediately.

Socket loss, discovery removal, failed dispatch, or `SubscriberDead` drains that endpoint's vector
exactly once before removing it. This reclaims the reference owned by a live `SampleView` when its
subscriber process is killed. Generation validation prevents a delayed stale release from changing
a later allocation at the same chunk index. Tests observed one-chunk `PoolExhausted` while all views
were live, then successful allocation and publication after one subscriber was killed and surviving
views released.

Other subscribers remain connected and continue receiving. A replacement subscriber is discovered
without restarting the publisher or registry.

## Robust Mutex And Owner-Death Recovery

Queue layout version 2 uses `PTHREAD_PROCESS_SHARED | PTHREAD_MUTEX_ROBUST`. Condition waits remain
monotonic. `EOWNERDEAD` means the previous owner may have stopped during a ring mutation, so repair
resets head/tail/size to a valid empty state, increments the recovery counter, broadcasts waiters,
and calls `pthread_mutex_consistent`. Publisher outstanding tracking repairs references for any
dropped entries. `ENOTRECOVERABLE` becomes an explicit error.

A fork-based unit test locked the shared mutex, corrupted ring invariants, and exited without
unlocking. The parent received owner-death recovery, observed a valid empty queue, and reused it.

On dead subscriber cleanup, the registry robust-closes the exact queue and broadcasts the
condition. A publisher blocked with `BLOCK_WITH_TIMEOUT` woke after the subscriber was killed,
returned a non-success result without waiting five seconds, then delivered to a replacement.

## SHM Cleanup Strategy

Dead publisher cleanup calls `shm_unlink` for the exact advertised pool. Dead subscriber cleanup
robust-closes then unlinks the exact queue and unlinks the registered data path only if `lstat`
still identifies a socket. `ENOENT` is accepted. No wildcard deletion or startup scavenger exists.

Final clean Debug, ASan, and UBSan runs left no `/mw_p5_*` pool, `/mw_q5_*` queue, or Phase 6 test
socket. No manual resource cleanup was needed after the acceptance runs.

## Session And Generation Safety

Node/session IDs are assigned by the running registry and heartbeats are bound to their dedicated
connection. PIDs are not trusted. Pool/queue IDs include PID plus process-local monotonic values for
naming, but registry descriptors and session records determine ownership. Chunk handles retain pool
ID, index, generation, and offset validation, so recovered/reused chunks reject stale releases.

## Reconnect Flow

SHM publishers resolve on each publish, reconcile the full subscriber endpoint set, preserve live
connections, release removed endpoints, and connect new endpoints. Failed endpoint IDs remain
tombstoned only until discovery removes them. Subscribers reset only the dead pool and accept the
next publisher data connection. Real crash tests replaced both peer types repeatedly while the same
registry and survivor remained active.

## Thread Model

`mw_registryd` remains one `epoll` event-loop thread. Each registry-enabled application session has
one joined control-only heartbeat thread. Publisher and subscriber API callers perform discovery,
pool allocation, queue operations, peer-event application, reference repair, and data socket I/O.
No background data-plane worker, lock-free queue, or real-time scheduler was added.

## Error Handling And Metrics

Invalid liveness ordering is rejected at construction/CLI parsing. Malformed or unauthenticated
heartbeat messages return existing explicit control errors. Robust-mutex failure, missing/invalid
SHM, queue close/full/timeout, pool exhaustion, stale handles, and peer disconnect retain explicit
errors. Destructors and crash cleanup are best effort and non-throwing where required.

Registry metrics count received heartbeats, suspected transitions, and dead-node cleanup. Queue
metrics count owner-death recovery and peer resets alongside existing policy counters. `mwctl`
exposes state, but no full metrics exporter or visualization was added.

## Acceptance Results

| Acceptance | Result | Evidence |
| --- | --- | --- |
| Clean Debug configure/build | PASS | GCC 13.3.0, C++17, warning-enabled build |
| CTest | PASS | 75/75 tests, zero failures |
| ASan | PASS | 75/75, leak detection and halt-on-error enabled |
| UBSan | PASS | 75/75, halt-on-error and stack traces enabled |
| TSan | NOT RUN | Optional; process-shared robust pthread support is not reliably modeled |
| Install/export | PASS | Installed package exports `mw::mw_core` |
| External consumer | PASS | Configured independently and printed `0.1.0` |
| Publisher `SIGKILL` | PASS | Pool unlinked; survivor reconnects across repeated replacement |
| Subscriber `SIGKILL` | PASS | Queue/socket removed; publisher and registry remain alive |
| Crash with live `SampleView` | PASS | Outstanding reference recovered and chunk reused |
| Remaining subscribers continue | PASS | Three survivors receive after fourth is killed |
| Publisher reconnect | PASS | Same subscriber accepts two killed publishers and final replacement |
| Subscriber reconnect | PASS | Same publisher discovers replacement endpoint |
| Open-socket heartbeat timeout | PASS | Node becomes DEAD with primary connection held open |
| SUSPECTED -> ALIVE | PASS | Valid heartbeat restores ALIVE before dead timeout |
| Robust mutex owner death | PASS | Real child exit while holding/corrupting mutex |
| BLOCK + dead subscriber | PASS | Blocked publish wakes after `SIGKILL` |
| Refcount/chunk recovery | PASS | One-chunk pool exits exhaustion and reallocates |
| Normal release/dead cleanup race | PASS | Queued release plus immediate `SIGKILL` releases once |
| Queue exhaustion regression | PASS | All Phase 5 overflow/timeout tests retained |
| Pool exhaustion regression | PASS | Existing allocator and view-hold tests retained |
| Invalid message size | PASS | Existing oversize/configuration tests retained |
| Topic type mismatch | PASS | Registry mismatch test retained |
| Duplicate node/publisher | PASS | State liveness/regression tests pass |
| Repeated crash/reconnect | PASS | Two cycles per test plus 20 full Phase 6 stress repetitions |
| No manual acceptance cleanup | PASS | Final namespace/socket audit empty after test suites |

## Known Limitations

- Existing contexts do not reconnect after registry-daemon/control-session failure.
- A live node that stops heartbeating is intentionally expired by lease semantics.
- Robust owner-death repair resets uncertain queue contents and can drop samples.
- Cleanup covers exact registered resources only; unrelated stale namespace entries are untouched.
- The bounded peer-event cache drops oldest entries under extreme churn; discovery and socket state
  remain fallback current-state sources.
- Shared robust pthread and atomic layouts assume a compatible local Linux/compiler ABI.
- No ROS2 adapter, benchmark framework, final metrics visualization, persistence, security, or
  distributed recovery is included.

## Phase Boundary

Phase 7 was not implemented.
