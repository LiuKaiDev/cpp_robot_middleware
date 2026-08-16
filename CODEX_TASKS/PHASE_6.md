# Codex Task - Phase 6: Robustness, Heartbeat, And Crash Recovery

## Scope

Implement Phase 6 only: add Linux-local node heartbeats, monotonic ALIVE/SUSPECTED/DEAD liveness,
control-connection failure detection, exact dead-node resource cleanup, bounded outstanding chunk
reference recovery, robust subscriber queue owner-death handling, and publisher/subscriber reconnect
after process crashes. Preserve all Phase 0-5 APIs and behavior.

## Required Architecture

- Return unique node/session identity at registration and authenticate a dedicated heartbeat
  connection with both values; do not trust PID alone.
- Keep the registry as a single-threaded control-plane event loop. Add one RAII heartbeat thread per
  registry session, but keep pool, queue, connection, and refcount mutation on application threads.
- Use `steady_clock` with configurable positive interval < suspect < dead timing and injectable
  timestamps in `RegistryState` tests.
- Treat primary control EOF/HUP as immediate death and a missed lease as
  ALIVE -> SUSPECTED -> terminal DEAD.
- Build idempotent cleanup from exact registered pool, queue, socket, node, topic, and endpoint
  metadata. Do not scan namespaces.
- Track outstanding handles per subscriber endpoint with a hard bound derived from finite pool
  capacity. Release each obligation exactly once after valid release, disconnect, discovery removal,
  dispatch failure, or dead-subscriber event.
- Use a robust process-shared queue mutex. On `EOWNERDEAD`, repair to a valid empty ring, broadcast
  waiters, and make the mutex consistent; report `ENOTRECOVERABLE` explicitly.
- Preserve surviving subscribers and allow replacement publishers/subscribers without restarting
  the registry or surviving processes.

## Verification

- Test deterministic liveness transitions, SUSPECTED recovery, terminal DEAD identity, timing
  validation, exact/idempotent cleanup, duplicate node, and duplicate publisher behavior.
- Fork and kill a real robust-mutex owner after corrupting queue indices; verify repair and reuse.
- Use real `SIGKILL` against publishers and subscribers, including a live `SampleView`, blocked
  publisher, one-chunk pool exhaustion/recovery, four subscribers, repeated publisher replacement,
  SHM unlink, queue unlink, and registry survival.
- Retain queue/pool exhaustion, message size, type mismatch, normal exit, Phase 0-5, install/export,
  and external-consumer regressions.
- Run clean Debug, ASan, and UBSan suites. TSan is optional for process-shared primitives.
- Add `docs/FAILURE_MODEL.md`, update relevant documentation, and produce `PHASE_6_REPORT.md`.

## Deferred Work

Do not implement distributed discovery, reliable retransmission, persistence, security, arbitrary
corruption repair, ROS2 adapters, benchmark/profiling infrastructure, final metrics visualization,
or Phase 7 work. `eventfd` and lock-free queues remain deferred.
