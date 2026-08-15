# Subscriber Queues And Loaned Shared Memory

## Scope

Phase 5 adds a bounded queue per SHM subscriber, explicit overflow behavior, writable publisher
loans, and read-only subscriber views. The queue contains metadata only. Payload bytes remain in the
publisher-owned memory pool documented in [MEMORY_POOL.md](MEMORY_POOL.md).

The copied UDS baseline and the ordinary SHM `Publisher::publish()` path remain available.

## Subscriber Queue Architecture

Every SHM `Subscriber` creates one POSIX SHM queue before registering its endpoint. The queue name,
ID, exact segment size, capacity, layout version, overflow policy, and block timeout are sent to the
registry. Discovery returns that descriptor to the publisher, which opens a read-write mapping.
The registry stores descriptor bytes only; it never maps or owns a queue.

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
mark, close state, attachment count, policy, timeout, counters, one process-shared mutex, and one
process-shared condition variable.

Each entry is only:

```text
pool_id, chunk_index, generation, payload_offset
```

No payload bytes, pointers, vectors, strings, or ownership-bearing C++ objects are stored in the
ring. Head/tail arithmetic is modulo capacity. Empty and full are distinguished by the explicit
size field.

## Synchronization Model

Queue mutation uses a `PTHREAD_PROCESS_SHARED` mutex. Producers waiting for space use a
`PTHREAD_PROCESS_SHARED` condition variable configured with `CLOCK_MONOTONIC`. A dequeue signals
one blocked producer; close broadcasts to all waiters. All condition waits recheck the full/closed
predicates while holding the mutex.

There is no lock-free queue, robust mutex recovery, owner-death repair, data-plane worker thread,
or hard real-time claim. Publisher and subscriber application threads perform queue operations.
The registry retains its single `epoll` thread.

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
reference counts or free lists. Subscriber releases are fixed metadata frames.

## UDS Wake And Release

The existing data UDS remains the notification channel. A 272-byte wake contains the pool
descriptor and queue ID; it contains no handle or business payload. A 32-byte release contains one
`ChunkHandle` and no payload. A wake is sent only for an empty-to-nonempty transition. The
subscriber reads the ring as the source of truth, so one wake can cover multiple queued entries.

`eventfd` optimization is deferred. UDS remains the supported Phase 5 wake mechanism.

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

## Normal Cleanup And Crash Boundary

Normal subscriber destruction closes the queue, wakes blocked producers, drains queued handles,
and emits their releases. Existing `SampleView` objects retain their release channel and release
later. Normal publisher destruction waits a bounded interval for outstanding view releases and
then drains remaining queued handles before unmapping and unlinking its pool.

Phase 5 does not repair a queue or reference count after `SIGKILL`, detect dead endpoints, reclaim
an ambiguous dead-subscriber reference, or scavenge orphaned SHM objects. It has no heartbeat,
lease, suspected/dead state, robust-mutex recovery, ROS2 adapter, or benchmark conclusion. Those
boundaries remain for later phases.
