# Subscriber Queues And Loaned Shared Memory

## Scope

Each SHM subscriber has a bounded queue with explicit overflow behavior; publishers can expose
writable loans and subscribers can retain read-only views. The process-shared queue mutex is robust
and participates in crash repair. The queue contains metadata only. Payload bytes remain in the
publisher-owned memory pool documented in [MEMORY_POOL.md](MEMORY_POOL.md).

The copied UDS baseline and the ordinary SHM `Publisher::publish()` path remain available.

## Subscriber Queue Architecture

Every SHM `Subscriber` creates one POSIX SHM queue before registering its endpoint. The queue name,
ID, exact segment size, capacity, layout version, overflow policy, and block timeout are sent to the
registry. Discovery returns that descriptor to the publisher, which opens a read-write mapping.
The registry stores the descriptor and maps the queue only when it must robust-close a dead
subscriber's registered queue.

```text
Publisher pool                         Subscriber endpoint
+--------------------+                 +---------------------------+
| ChunkHeader/payload| <--- handle --- | fixed-capacity ring queue |
+--------------------+                 +---------------------------+
         ^                                      ^
         | release                              | empty->nonempty wake
         +------------ data UDS ----------------+
```

The subscriber owns the queue SHM name and unlinks it at normal destruction. An attachment count
keeps the process-shared synchronization objects alive until the final normal mapping detaches.
The publisher owns its mapping only. Queue capacity is bounded to 65,536 entries; depth zero and
checked-size overflow are rejected.

## Ring Buffer Layout

The native local-host layout consists of `SubscriberQueueHeader`, alignment padding, and exactly
`capacity` `ChunkHandle` slots. Header state includes head, tail, current size, capacity, high-water
mark, close state, attachment count, policy, timeout, policy/repair counters, one robust
process-shared mutex, and one process-shared condition variable. Queue layout version 3 adds exact
blocked-operation and blocked-time counters used by the benchmark; it does not change the queue
policies.

Each entry is only:

```text
pool_id, chunk_index, generation, payload_offset
```

No payload bytes, pointers, vectors, strings, or ownership-bearing C++ objects are stored in the
ring. Head/tail arithmetic is modulo capacity. Empty and full are distinguished by the explicit
size field.

## Synchronization Model

Queue mutation uses a `PTHREAD_PROCESS_SHARED | PTHREAD_MUTEX_ROBUST` mutex. Producers waiting for space use a
`PTHREAD_PROCESS_SHARED` condition variable configured with `CLOCK_MONOTONIC`. A dequeue signals
one blocked producer; close broadcasts to all waiters. All condition waits recheck the full/closed
predicates while holding the mutex.

On `EOWNERDEAD`, the acquiring process treats ring indices/size as uncertain, resets the queue to
empty, increments `owner_death_recoveries`, broadcasts the condition, and calls
`pthread_mutex_consistent`. This may drop queued handles; publisher-side outstanding tracking is the
authority that repairs their references. `ENOTRECOVERABLE` becomes an explicit synchronization
error. There is no lock-free queue, data-plane worker thread, or hard real-time claim. Publisher and
subscriber application threads perform normal queue operations; the registry event loop performs
only dead-subscriber close/repair.

## Queue Full Semantics

`SubscriberConfig` provides `queue_depth`, `overflow_policy`, and `block_timeout`. Policy applies
per endpoint, so subscribers on one topic may choose different capacities and behavior.

### DROP_NEWEST

When full, the existing ring is unchanged and the new handle is rejected for that subscriber.
The publisher gives back the tentative reference and reports `QueueFull` if every endpoint rejects
the sample. `PublishResult::dropped_newest` counts endpoint decisions.

### DROP_OLDEST

When full, the queue removes its oldest handle and inserts the new one atomically under the queue
mutex. The publisher then removes and releases exactly the displaced endpoint reference. If it was
the final reference, the old chunk is reclaimed. `PublishResult::dropped_oldest` records the
replacement; the publish remains successful because the newest sample was accepted.

### BLOCK_WITH_TIMEOUT

When full, the publisher waits on the monotonic condition deadline until a dequeue creates space,
the queue closes, or `block_timeout` expires. Expiry rejects only that endpoint reference and
reports `QueueTimeout` when no endpoint accepted the sample. This timeout is queue backpressure,
not dead-subscriber detection.

## Reference And Enqueue Protocol

Publication starts with one publisher guard reference. For each subscriber, the publisher adds a
tentative reference before exposing the handle to its queue. Acceptance transfers that reference
to the endpoint. Drop, timeout, close, synchronization failure, or failed first wake immediately
returns it. `DROP_OLDEST` additionally returns the displaced endpoint reference.

Only after every endpoint decision does the publisher release the guard. Therefore a fast
subscriber cannot release the new generation to zero and allow reuse while the publisher is still
adding references for later subscribers.

The publisher tracks outstanding handles per connection and is the only process that mutates pool
reference counts or free lists. The vector is bounded by the total configured pool chunk count.
Subscriber releases are fixed metadata frames. Dead-subscriber cleanup drains that endpoint's
remaining vector exactly once and tombstones the endpoint until discovery removes it.

## UDS Wake And Release

The existing data UDS remains the notification channel. A 272-byte wake contains the pool
descriptor and queue ID; it contains no handle or business payload. A 32-byte release contains one
`ChunkHandle` and no payload. A wake is sent only for an empty-to-nonempty transition. The
subscriber reads the ring as the source of truth, so one wake can cover multiple queued entries.

`eventfd` optimization is deferred. UDS remains the supported wake mechanism.

## LoanedSample Lifecycle

`Publisher::loan(size)` is available only for SHM publishers:

```text
FREE --loan--> LOANED --LoanedSample::publish--> PUBLISHED
  ^                |
  +---- cancel ----+  LoanedSample destructor without publish
```

`LoanedSample` is move-only and owns exactly one LOANED generation. `data()` exposes only the
payload region for application writes; pool headers and lifecycle fields remain private. `size()`
is the requested payload size and `capacity()` is the selected size-class capacity. Destruction
without publish cancels and returns the chunk. A move transfers ownership and leaves the source
inactive. The first `publish()` consumes the object whether it succeeds or fails; a second call
returns `InvalidState` and cannot enqueue or add references again.

On UDS, `loan()` returns `UnsupportedTransport`; it does not allocate a heap buffer that pretends to
be a shared-memory loan.

## SampleView Lifecycle

`Subscriber::takeView()` and `waitAndTakeView()` expose the SHM receive path. `SampleView` is
move-only and provides only `const void* data() const`, size, sequence, monotonic publish timestamp,
and logical chunk identity. It never exposes writable shared payload memory.

One view owns one subscriber reference. It retains a shared read-only pool mapping and a shared
release-channel context rather than a raw `Subscriber*`. Its non-throwing destructor sends one
release exactly once. Moved-from views are inert. Consequently a view can safely outlive its
`Subscriber` object and continues to hold the chunk against reuse until its own destruction.

## Copy And Loan Paths

The SHM copy path remains:

```text
Publisher::publish(application buffer)
  -> one middleware memcpy into pool chunk
  -> subscriber queue
  -> SampleView, or one copy into compatible ReceivedMessage
```

The verified loan path is:

```text
application writes LoanedSample::data()
  -> publisher publishes that same logical pool/chunk/generation/offset
  -> subscriber reads it through SampleView
```

The verified SHM loaned path avoids middleware payload copies between publisher loan fill and
subscriber `SampleView`. This statement does not apply to UDS, ordinary `publish()`, or the owning
`ReceivedMessage` compatibility API. Virtual addresses are not compared across processes; logical
pool ID, chunk index, generation, and payload offset identify the same payload.

## Normal And Crash Cleanup

Normal subscriber destruction closes the queue, wakes blocked producers, drains queued handles,
and emits their releases. Existing `SampleView` objects retain their release channel and release
later. Normal publisher destruction waits a bounded interval for outstanding view releases and
then drains remaining queued handles before unmapping and unlinking its pool.

After subscriber `SIGKILL`, control EOF/HUP or heartbeat timeout removes its endpoint. The registry
opens the exact registered queue, robust-recovers the mutex if necessary, marks the queue closed,
broadcasts blocked producers, and unlinks the name. A peer-death event tells the publisher to
release all still-outstanding endpoint references. Other subscribers and their queues are left
untouched. Repeated cleanup tolerates missing records and already-unlinked names.

After publisher death, the registry unlinks the exact pool name and tells subscribers to discard
only entries for that pool and reset the old mapping/connection. A replacement publisher may then
connect and install a new pool descriptor. Recovery does not repair arbitrary queue memory
corruption, recover a failed registry daemon in-place, or claim hard real-time behavior. The
separate ROS2 adapter preserves these queue semantics while adding serialization copies.
