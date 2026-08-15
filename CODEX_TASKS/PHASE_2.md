# Codex Task - Phase 2: Registry, Discovery, and mwctl

## Scope

Implement Phase 2 only: add a Linux-local registry control plane and automatic endpoint discovery
while preserving the Phase 1 copied-payload Unix Domain Socket data plane and direct mode.

## Required Architecture

- Build `mw_registryd` as a single-threaded `epoll` server on a configurable UDS path, defaulting
  to `/tmp/mw_registry.sock`.
- Separate `ControlProtocol`, `RegistryState`, `RegistryServer`, and `RegistryClient` concerns.
- Use an explicitly encoded, fixed-width 16-byte `ControlHeader`, correlated request IDs, bounded
  payloads, explicit error codes, and partial read/write handling.
- Support node register/unregister, topic advertise/unadvertise, topic subscribe/unsubscribe,
  endpoint resolution, node/topic listing, and topic query.
- Assign monotonic `node_id`, `topic_id`, and `endpoint_id` values.
- Match a topic only when both `type_name` and `type_hash` match. Reject a second active publisher
  for one topic and retain multiple subscriber records.
- Support publisher-first and subscriber-first startup without restarting the first endpoint.
- Keep payload transfer on the Phase 1 UDS frame transport; the registry never forwards payloads.
- Preserve the Phase 1 `Context(node_name)` direct mode and add an explicit registry-enabled mode.

## Programs And Tests

- Build `mwctl node list`, `mwctl topic list`, and `mwctl topic info <topic>` as ordinary control
  protocol clients.
- Extend ping demos with configurable registry, topic, and type options while retaining direct use.
- Unit-test control encoding/validation and registry state transitions, identity, matching, type
  mismatch, single-publisher enforcement, cleanup, and queries.
- Integration-test a real daemon plus real publisher/subscriber processes in both startup orders,
  type mismatch, live `mwctl`, malformed/truncated requests, and state integrity.
- Retain and run every Phase 1 regression test.
- Run a clean Debug build, full CTest, ASan, UBSan, manual process acceptance, install/export, and
  external-consumer verification.

## Documentation And Delivery

- Add `docs/CONTROL_PLANE.md`.
- Update `README.md` to Phase 2 status.
- Add `PHASE_2_REPORT.md` with actual acceptance results and limitations.
- Review status/diff, commit on `main`, and attempt a normal push only after all acceptance passes.

## Prohibited Phase 3+ Work

Do not add shared memory, `shm_open`, `mmap`, memory pools/chunks, reference counting, subscriber
queues, backpressure policies, loaned samples, heartbeat, timeout-based dead-process detection,
crash recovery, `eventfd`, `SCM_RIGHTS`, ROS2, benchmark infrastructure, or lock-free structures.
