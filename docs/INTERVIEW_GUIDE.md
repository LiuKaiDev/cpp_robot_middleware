# Interview Guide

## Project Story

This project asks what happens beneath a robotics Pub/Sub API. It starts with a copied UDS
baseline, separates registry/discovery from payload delivery, adds a preallocated SHM pool and
per-subscriber queues, makes lifecycle and backpressure explicit, recovers registered resources
after process failure, reconnects through a ROS2 adapter, and measures the resulting tradeoffs.

## Design Questions

### Why Pub/Sub?

Robot producers and consumers should depend on a typed topic contract rather than direct process
identity. The registry matches endpoints while the data plane remains direct between processes.

### Why Registry And Discovery?

Hardcoded socket paths do not model nodes, types, startup order, or replacement. `mw_registryd`
assigns identities, enforces one active publisher, validates type/transport compatibility, and
returns exact data endpoints.

### Control Plane Versus Data Plane?

Registration, discovery, heartbeat, stats, and cleanup use the registry UDS control plane. UDS
payload frames or SHM chunks/queues form the direct data plane. The registry never copies payloads.

### Why One Publisher Per Topic?

V1 concentrates on multi-process IPC, N-reader lifetime, and backpressure without introducing MPSC
ordering, competing pool ownership, or publisher arbitration.

### Why Shared Memory?

Large socket payloads cross a kernel boundary for every subscriber. SHM maps one publisher-owned
payload into subscriber processes and fans out fixed handles. It trades simpler copies for explicit
allocation, synchronization, and crash recovery.

### Why Does SHM Not Automatically Mean Zero-Copy?

Ordinary `publish(data, size)` still copies into the chunk. Owning receive copies out. Only the
verified native `LoanedSample` fill to `SampleView` read path avoids middleware payload copies.

### SHM Copy Versus SHM Loan?

Copy accepts an existing application buffer and performs one `memcpy`. Loan lets the application
write the pool chunk directly. In the optimized reference, 4 MiB p50 was 626.9 us Copy versus
274.1 us Loan; throughput was 1500.0 versus 1524.6 MiB/s on the measured host.

### Why A Memory Pool And Size Classes?

The hot path reuses fixed chunks rather than mapping or allocating payload storage per message.
Smallest-fitting classes bound memory and fragmentation. Exhaustion is explicit instead of falling
back to hidden unbounded allocation.

### Chunk Lifecycle And Generation?

The stored state is `FREE -> LOANED -> PUBLISHED -> RELEASED -> FREE`. A generation changes on each
allocation, preventing an old release for the same index from affecting new payload ownership.

### How Do N Subscribers Share One Payload?

Each subscriber queue stores the same logical handle, not copied bytes. The publisher owns one
reference obligation per accepted endpoint and reclaims the chunk after every valid release or
crash-time repair.

### Why A Ring Buffer?

Fixed-capacity metadata storage makes memory use and full behavior explicit. V1 uses a robust
process-shared mutex and condition variable because correctness and recovery were prioritized over
an unprofiled lock-free redesign.

### Backpressure Policies?

`DROP_NEWEST` preserves queued work, `DROP_OLDEST` favors the most recent sample, and
`BLOCK_WITH_TIMEOUT` waits for space without permitting permanent producer deadlock. Policy is per
subscriber.

## Failure Questions

### How Is Failure Detected?

Primary control EOF/HUP triggers immediate cleanup. A separate heartbeat lease covers a process
that retains the socket but stops renewing: ALIVE becomes SUSPECTED, then DEAD.

### Why A Robust Process-Shared Mutex?

If a process dies while mutating a queue, the next locker gets `EOWNERDEAD`. Queue indices are
treated as uncertain and reset; publisher outstanding-handle tracking repairs references for
discarded entries.

### What Happens To An Outstanding SampleView After Subscriber SIGKILL?

The publisher's per-endpoint obligation remains authoritative. A dead-subscriber event drains it
exactly once. Generation checks reject any later stale release.

### Can The System Recover If The Registry Dies?

Not automatically inside existing contexts. Registry restart/reconnection is an explicit known
limitation.

## Performance Questions

### Why Can UDS Be Competitive For Small Messages?

Fixed SHM coordination can outweigh a small socket payload. In the optimized reference, 64-byte medians were
80.7 us UDS, 170.0 us SHM Copy, and 157.2 us SHM Loan; message rates were 148.0k, 130.8k, and
151.4k/s respectively. There is no universal winner.

### Where Does SHM Benefit Large Messages?

At 4 MiB, measured p50 was 2053.4 us UDS, 626.9 us Copy, and 274.1 us Loan. Delivered throughput
was 1113.0, 1500.0, and 1524.6 MiB/s. The benefit comes with much larger finite SHM mappings and
explicit queue/lifecycle work.

### What Are p50 And p99?

p50 is the median typical observation; p99 exposes the slow tail. Fixed-rate latency cases and
throughput sampling must not be mixed, and a small 4 MiB sample count makes tail comparisons noisy.

### How Were CPU And RSS Measured?

The runner snapshots `/proc/<pid>/stat` CPU ticks at acknowledged measurement boundaries and
samples `/proc/<pid>/status` RSS every 100 ms. Publisher and subscribers remain separate.

### What Did Profiling Find?

Small-message SHM was making a synchronous registry resolve for every publication. Benchmark and
context-switch evidence plus source inspection justified a bounded 1 ms reuse window. Removing
that cost exposed redundant wake accumulation, which was fixed by nonblocking wake draining.
`perf` and `strace` were unavailable, so no symbol or syscall-time percentages are claimed.

## ROS2 Questions

### Why An Adapter Instead Of A Custom RMW?

The project demonstrates integration without taking ownership of DDS discovery, QoS mapping, and
the full RMW contract. The separate adapter uses normal ROS2 APIs and links the installed core.

### Adapter Versus RMW?

An adapter explicitly bridges selected ROS types and topics at application level. An RMW is ROS2's
general middleware implementation boundary. This project is the former and supports exactly
String, Twist, and Image.

## Honest Closing

The strongest evidence is not a claim that SHM always wins. It is the combination of explicit copy
paths, bounded ownership, fault tests, a complete non-cherry-picked matrix, an evidence-backed
optimization, and documented limits. See [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md).
