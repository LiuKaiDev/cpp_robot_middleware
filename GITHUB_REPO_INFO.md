# GitHub Repository Information

## Recommended repository settings

- Repository name: `cpp_robot_middleware`
- Display title: `C++ High-Performance Pub/Sub Middleware & ROS2 Adapter`
- Visibility: `Public` (recommended for portfolio / recruiting use)
- Default branch: `main`
- Initialize repository on GitHub:
  - Add README: **No**
  - Add .gitignore: **No**
  - Choose a license: **No for the initial empty repository**
- Suggested license later: MIT, after you decide the copyright holder/name.

## GitHub description

Linux + C++17 local Pub/Sub middleware with a Unix Domain Socket control plane, shared-memory data plane, ROS2 adapter, and reproducible benchmarks.

## Suggested topics

`cpp17`, `linux`, `ipc`, `shared-memory`, `unix-domain-sockets`, `pubsub`,
`middleware`, `robotics`, `ros2`, `cmake`, `concurrency`, `memory-pool`

Do not add `zero-copy` as a repository topic until the loaned-sample data path is
actually implemented and verified end-to-end.

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
