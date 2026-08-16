# Failure Model

## Scope

The v1 failure model covers one Linux host, one running `mw_registryd`, one active publisher per
topic, and N subscribers. It detects middleware process failure through control-socket EOF/HUP or
a missed heartbeat lease. It repairs registry state and the exact POSIX SHM/socket resources
registered by that process. Tests use real `SIGKILL` for publisher and subscriber failure.

It does not provide distributed consensus, persistence, retransmission, exactly-once delivery,
hard real-time deadlines, or recovery from host/kernel failure and arbitrary memory corruption.

## Heartbeat Architecture

Each registry-enabled `Context` shares one `RegistrySession`. Registration returns a monotonically
assigned `node_id` and `session_id`. The session then opens a dedicated control connection,
attaches it to that identity, and starts one RAII heartbeat thread. The primary connection remains
available for synchronous registration/discovery calls, including a discovery request that waits
for the first subscriber.

The heartbeat carries no payload data. Its response contains the current ALIVE state and bounded,
deduplicated peer-death metadata. Publisher and subscriber application threads consume those events
before data-plane operations; the heartbeat thread never changes queues, pools, connection vectors,
or chunk reference counts.

Session destruction signals the thread, wakes its condition variable, joins it, closes the
heartbeat connection, and normally unregisters through the primary connection.

## Liveness State Machine

The registry uses `std::chrono::steady_clock` only:

```text
REGISTER or valid HEARTBEAT
          |
          v
        ALIVE -- suspect timeout --> SUSPECTED
          ^                            |
          | valid heartbeat            | dead timeout
          +----------------------------+----> DEAD -> record removed
```

DEAD is terminal for that node/session. A later process must register a new session. A heartbeat
while SUSPECTED restores ALIVE. `mwctl node list` shows ALIVE/SUSPECTED; DEAD nodes are absent.

Default timing is:

| Setting | Default |
| --- | ---: |
| heartbeat interval | 250 ms |
| suspect timeout | 750 ms |
| dead timeout | 1500 ms |

Configuration must be positive and satisfy interval < suspect < dead. `mw_registryd` accepts
`--heartbeat-interval-ms`, `--suspect-timeout-ms`, and `--dead-timeout-ms`. Unit tests inject exact
steady-clock time points instead of sleeping.

## Control Connection Loss

EOF, HUP, or an unrecoverable error on the primary control connection immediately removes its node
and runs the same idempotent cleanup used by timeout death. It does not wait for the heartbeat
deadline. Losing only the heartbeat connection detaches it; if the primary remains open but no
renewal arrives, the normal SUSPECTED/DEAD timeouts apply.

## Session Identity

A heartbeat is accepted only when its connection was attached using the matching node ID and
unique session ID. Node names remain unique among live records. PID may appear in generated local
resource names but is not trusted as session identity because operating systems reuse PIDs.

## Registry Cleanup

Before removing a dead node, `RegistryState` captures a bounded `DeadNodeCleanup` plan containing:

- publisher pool descriptors;
- subscriber queue descriptors and data socket paths;
- publisher/subscriber endpoint IDs and targeted peer-death events;
- primary and heartbeat connection identities.

The server then removes exact node/topic/endpoint records, cancels affected pending discovery,
closes the companion control connection, performs resource cleanup, and queues events for live
peers. It never scans `/dev/shm` or `/tmp`. Repeated cleanup accepts absent records, closed
descriptors, `ENOENT`, and already-unlinked names.

## Publisher Crash

For a dead SHM publisher the registry unlinks its exact advertised pool name and sends
`PublisherDead` with the old pool ID to each surviving subscriber endpoint. A subscriber discards
only queued handles for that pool, closes the old release channel, drops its old pool mapping, and
waits for a replacement publisher. If a new pool handle arrived just before old-socket HUP, it stays
queued until the replacement wake validates and installs its pool descriptor.

Unlinking a pool name does not invalidate an already-open mapping or live `SampleView`; those local
objects retain their mappings until destruction. A LOANED chunk dies with its publisher and needs no
external reference repair because the whole pool is removed.

## Subscriber Crash

For a dead SHM subscriber the registry robust-opens the exact registered queue, repairs the mutex
when required, marks the queue closed, broadcasts blocked producers, unlinks the queue, and removes
the registered data socket path only when it is still a socket. It sends `SubscriberDead` to the
live publisher endpoint. Other subscribers and queues are unchanged.

## Outstanding Reference Tracking

The publisher is the only chunk refcount writer. Each connected endpoint owns a vector of published
handles that have not yet produced a valid release. Its reserved and enforced maximum is the total
finite chunk count in the publisher pool, so crash bookkeeping is bounded.

A normal release validates pool ID, chunk index, generation, and offset, decrements one reference,
and erases one matching obligation. Drop/timeout/failed-wake paths return tentative references
immediately. Discovery removal, socket loss, failed dispatch, or `SubscriberDead` drains all
remaining endpoint obligations exactly once before removing the connection. Endpoint tombstones
prevent duplicate cleanup until the registry no longer advertises that endpoint.

This repairs a chunk held by a `SampleView` when its subscriber process is killed. The same chunk
can return to FREE and be allocated with a new generation. A delayed release for the old generation
cannot release the new allocation.

## Robust Process-Shared Mutex

Queue layout version 3 initializes the mutex with both `PTHREAD_PROCESS_SHARED` and
`PTHREAD_MUTEX_ROBUST`. Condition waits use `CLOCK_MONOTONIC`.

When lock or timed-wait acquisition reports `EOWNERDEAD`, ring metadata may be mid-update. Recovery
therefore resets head, tail, and size to a valid empty state, increments the owner-death metric,
broadcasts waiters, and calls `pthread_mutex_consistent`. Publisher outstanding tracking repairs
references for discarded handles. `ENOTRECOVERABLE` is reported as an explicit synchronization
error. A fork-based test kills a process while it owns and corrupts the mutex, then verifies repair
and continued queue use.

The registry's dead-subscriber close also broadcasts the condition. A publisher blocked under
`BLOCK_WITH_TIMEOUT` wakes rather than waiting the entire configured timeout.

## SHM Ownership And Cleanup

The publisher owns its pool name, mapping, free lists, chunk state, and reference counts. Each
subscriber owns its queue name, queue mapping, and listener path. Peers own non-name-owning
mappings. The registry stores exact descriptors so it can unlink an owner's resources after death;
it never assumes that similarly named objects belong to that node.

Normal exit remains owner-led RAII cleanup and explicit unregistration. Crash exit is
registry-led cleanup plus application-thread peer repair. Both routes converge on the same removed
registry state and tolerate the other route having already completed.

## Reconnect Flow

A live SHM publisher reuses its last compatible discovery result for at most 1 ms. An empty
connection set, socket/queue failure, disconnect, or peer-death event invalidates the window
immediately; otherwise the next bounded refresh removes vanished subscribers and connects new
endpoint IDs while preserving survivors. A live subscriber observes publisher death through its
old data socket or peer event, resets only the old pool, and accepts a replacement connection.
Registry one-publisher enforcement allows a replacement only after the dead publisher record is
removed.

## Metrics

`RegistryState` records heartbeat receives, ALIVE-to-SUSPECTED transitions, and dead-node cleanup
count. Queue stats record owner-death recovery and peer reset counts in addition to the queue policy
counters. `mwctl stats` exposes a current registry-object snapshot and those three lifetime
liveness counters. Per-publication queue, drop, block, and allocation metrics remain in
`PublishResult` and benchmark artifacts; there is no general metrics export or visualization
system.

## Known Limitations

- An existing context does not reconnect its heartbeat/control session after registry-daemon loss.
- A live process that stops heartbeating is treated as dead even if one of its application threads
  still runs; this is lease semantics.
- Queue owner-death repair deliberately resets uncertain contents and can drop samples.
- Registry cleanup uses exact registered names only; unrelated or preexisting stale resources are
  not scavenged.
- The event cache is bounded and may drop the oldest event under extreme churn; discovery and
  socket reconciliation remain the fallback sources of current endpoint truth.
- Robust pthread behavior and shared atomic layout are local Linux/compiler ABI assumptions.
- The ROS2 adapter and automated benchmark reuse these cleanup guarantees; they do not extend the
  failure model to distributed hosts, registry restart recovery, or arbitrary memory corruption.
