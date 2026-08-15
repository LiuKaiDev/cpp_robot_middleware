# Codex Task - Phase 3: Shared Memory Data Plane V1

## Scope

Implement Phase 3 only: retain the Phase 1 copied UDS payload baseline and Phase 2 registry while
adding a registry-discovered POSIX shared-memory payload transport for one publisher and one
subscriber.

## Required Architecture

- Add explicit `TransportType::UnixDomainSocket` and `TransportType::SharedMemory` modes.
- Keep the registry control plane on UDS and add transport metadata compatibility checks.
- Keep the Phase 1 24-byte UDS payload frame unchanged.
- Use `shm_open`, `ftruncate`, `mmap`, `munmap`, and `shm_unlink` through a move-only RAII region.
- Use one SHM object per in-flight message with an explicitly encoded fixed-width header plus
  payload. Names must be valid, unique, and not contain raw topic strings.
- Send only bounded SHM locator metadata over the publisher/subscriber UDS; never duplicate the
  business payload there.
- Require a subscriber ACK after open/map/validation/copy before publisher-owned unlink.
- Preserve the owning `ReceivedMessage` API and document both remaining payload copies.
- Keep application-thread `publish()` and `waitAndTake()` execution without data worker threads.

## Required Verification

- Unit-test region creation/opening, mapping visibility, moves, invalid input, and cleanup.
- Unit-test segment header, notification, ACK, bounds, truncation, metadata, and notification size.
- Run a real registry, publisher process, and subscriber process in SHM mode.
- Verify deterministic UDS and SHM payload correctness at 1 KB, 64 KB, 1 MB, and 4 MB.
- Verify normal completion leaves no new project-owned `/dev/shm` object.
- Retain every Phase 0/1/2 regression and verify `mwctl`, install/export, ASan, and UBSan.
- Add `docs/DATA_PLANE.md`, update README/control-plane documentation, and produce
  `PHASE_3_REPORT.md` with actual results.

## Prohibited Phase 4+ Work

Do not implement a memory or chunk pool, reusable free list, multi-subscriber payload sharing,
reference-counted lifecycle, queue/backpressure policy, loaned sample/view API, `eventfd`,
`SCM_RIGHTS`, heartbeat/crash recovery, ROS2 adapter, benchmark infrastructure, or performance
claims. Do not call this transport zero-copy.
