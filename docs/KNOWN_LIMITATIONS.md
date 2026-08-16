# Known Limitations

These are current implementation boundaries, not planned features.

## Platform And Scope

- Linux-specific local IPC using Unix Domain Sockets, POSIX shared memory, `epoll`, and pthread
  process-shared synchronization.
- One host only. There is no remote transport or distributed discovery.
- One active publisher per topic and N subscribers. Multi-publisher ordering is not implemented.
- Shared native atomics and robust pthread objects assume compatible local Linux/compiler ABIs.
- Normal OS scheduling only; there is no hard real-time guarantee, affinity, isolation, or priority
  policy.

## Delivery Semantics

- Volatile best-effort behavior bounded by configured queue policies; no persistence, durability,
  exactly-once delivery, or retransmission protocol.
- `DROP_NEWEST` and `DROP_OLDEST` intentionally discard endpoint deliveries under pressure.
- Robust mutex owner-death recovery resets uncertain queue contents and may discard samples.
- UDS and SHM policy semantics are not a complete equivalent of ROS2 QoS.
- No security, authentication, authorization, or encryption.

## Copy Boundaries

- UDS copies payload through the socket path and is not zero-copy.
- SHM `Publisher::publish(data, size)` copies the application buffer once into a pool chunk.
- Owning `ReceivedMessage` copies bytes out of mapped memory.
- Only the native SHM `LoanedSample` to `SampleView` path was verified to avoid middleware payload
  copies.
- The ROS2 adapter serializes/deserializes and copies adapter buffers. It is not end-to-end
  zero-copy and is not a custom RMW.

## Failure Recovery

- Existing contexts do not automatically reconnect after `mw_registryd` itself is lost or
  restarted.
- Recovery covers registered process crashes, exact registered resources, and peer reconciliation;
  it does not repair host/kernel failure or arbitrary shared-memory corruption.
- Cleanup never scans all `/dev/shm` or `/tmp` names and cannot reclaim unrelated/preexisting stale
  resources.
- The bounded peer-event cache may discard oldest events under extreme churn; current discovery and
  socket state remain fallback sources.
- Direct, registry-free UDS mode cannot use registry crash cleanup.

## API And Schema

- Type compatibility is exact `type_name` plus `type_hash`; there is no IDL compiler, dynamic
  introspection, or schema conversion.
- Registry requests are synchronous and not multiplexed for concurrent calls on one session.
- ROS2 adapter support is limited to `std_msgs/msg/String`, `geometry_msgs/msg/Twist`, and
  `sensor_msgs/msg/Image` on ROS2 Jazzy.
- PointCloud2 and arbitrary ROS messages are not supported.

## Measurement

- The committed reference was measured once on WSL2 with an Intel i5-8300H and normal background
  scheduling. It is evidence for that configuration, not a universal transport ranking.
- Throughput-profile latency is systematically sampled and is not a complete tail distribution.
- Direct ROS2 uses normal `rmw_fastrtps_cpp`; its results include ROS serialization/DDS behavior.
- `perf` and `strace` were unavailable during Phase 8.1. Profiling used benchmark deltas, `/proc`
  counters, 100 ms wait-channel samples, and source inspection, so no symbol-level CPU or dynamic
  syscall ranking is claimed.
- RSS includes process/library mappings and finite configured SHM mappings; it is not only live
  payload bytes.

## Not Implemented

Lock-free/SPSC replacement, `eventfd`, `SCM_RIGHTS`, `memfd_create`, per-thread allocator caches,
custom CPU scheduling, multi-publisher semantics, TCP, custom RMW, DDS/RTPS, persistence, security,
and distributed recovery are not implemented. They belong to possible future investigation only
after evidence and explicit scope approval.

The project license has not been selected by the owner.
