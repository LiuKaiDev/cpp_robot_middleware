# GitHub Repository Information

## Recommended repository settings

- Repository name: `cpp_robot_middleware`
- Display title: `C++ High-Performance Pub/Sub Middleware & ROS2 Adapter`
- Visibility: `Public` (recommended for portfolio / recruiting use)
- Default branch: `main`
- Initialize repository on GitHub:
  - Add README: **No**
  - Add .gitignore: **No**
  - Choose a license: **No**
- Current status: no project license has been selected. The owner must choose the license and
  copyright holder explicitly.

## GitHub description

Linux + C++17 local Pub/Sub middleware with a Unix Domain Socket control plane, shared-memory data plane, ROS2 adapter, and reproducible benchmarks.

## Suggested topics

`cpp17`, `linux`, `ipc`, `shared-memory`, `unix-domain-sockets`, `pubsub`,
`middleware`, `robotics`, `ros2`, `cmake`, `concurrency`, `memory-pool`

Avoid a repository-wide `zero-copy` claim. Only the native SHM `LoanedSample` to `SampleView`
payload path is verified to avoid middleware payload copies; other paths retain documented copies.

## Branch / tag convention

- `main`: must remain buildable and testable.
- Phase branches:
  - `feat/phase-0-bootstrap`
  - `feat/phase-1-uds`
  - `feat/phase-2-registry`
  - `feat/phase-3-shm`
  - `feat/phase-4-memory-lifecycle`
  - `feat/phase-5-backpressure-loan`
  - `feat/phase-6-recovery`
  - `feat/phase-7-ros2-adapter`
  - `feat/phase-8-benchmark`
  - `feat/phase-9-docs-demo`
- Tags after acceptance: `phase-0` ... `phase-9`.

## Commit style

Keep commits small and purpose-oriented. Examples:

- `build: scaffold C++17 middleware project`
- `test: add phase 0 packaging smoke tests`
- `docs: add phase 0 report`
- `feat: add unix socket transport`
- `test: add socket frame tests`
- `docs: document uds transport`
