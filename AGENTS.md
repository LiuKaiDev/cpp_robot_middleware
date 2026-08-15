# AGENTS.md

## Repository mission

This repository implements a small, explainable, testable, benchmarkable
Linux + C++17 local multi-process Pub/Sub middleware for robotics.

The source of truth is `PROJECT_PLAN.md`.

## Mandatory workflow for every task

1. Read `PROJECT_PLAN.md`.
2. Read the current phase task under `CODEX_TASKS/`.
3. Inspect the current repository state before changing files.
4. Summarize what is already implemented.
5. Work only on the requested phase.
6. Before editing, state:
   - design for this phase,
   - files to add,
   - files to modify,
   - responsibility of each file,
   - important data structures,
   - resource ownership,
   - process/thread model,
   - error handling.
7. Then implement the phase.
8. Build and run all relevant tests.
9. Fix failures caused by the phase.
10. Run the phase acceptance commands.
11. Update documentation required by the phase.
12. Produce `PHASE_X_REPORT.md`.
13. Stop. Do not implement the next phase.

Do not run `git commit`, `git push`, merge branches, or create tags unless the
user explicitly asks you to do so.

## Architectural invariants

- Middleware Core must not depend on ROS2.
- Control plane and data plane remain separated.
- Linux single-host, multi-process IPC is the first-version scope.
- Correctness -> testability -> measurability -> profiling -> optimization.
- Do not introduce lock-free structures before benchmark/profiling evidence.
- Do not claim zero-copy unless the full payload path is actually copy-free.
- First version supports one active publisher per topic and N subscribers.
- Do not implement cross-machine discovery or distributed middleware in v1.

## Dependency policy

Core v1 should prefer:

- C++17 standard library,
- Linux/POSIX APIs,
- GoogleTest for tests.

Do not add protobuf, Boost IPC, ZeroMQ, iceoryx, DDS/Fast DDS Core, or similar
frameworks merely for convenience.

If a new dependency appears necessary, stop and explain:

1. why it is needed,
2. benefits,
3. costs,
4. whether it changes or violates `PROJECT_PLAN.md`.

Do not add it until the user approves.

ROS2 dependencies are allowed only inside the ROS2 adapter phase/layer.

## C++ engineering rules

- C++17.
- Prefer RAII and explicit ownership.
- Prefer move-only wrappers for OS resources where appropriate.
- Avoid global raw owning pointers.
- Avoid scattered manual `close()` / `munmap()` in business logic.
- Avoid uncontrolled `new` / `delete`.
- Avoid God classes and giant `main.cpp` implementations.
- Use small interfaces and explicit errors.
- Keep resources bounded where the design calls for bounded resources.
- Compile with at least `-Wall -Wextra -Wpedantic` for project code.

## Scope guardrails for v1

Do not implement in the first version unless a later explicit phase changes the
scope:

- full DDS / RTPS,
- custom ROS2 RMW,
- DDS discovery,
- distributed/cross-machine auto-discovery,
- persistence/durability/exactly-once,
- reliable retransmission protocol,
- security/authentication/encryption,
- IDL compiler or schema generator,
- RDMA/DPDK/GPU DMA,
- hard real-time guarantees,
- full ROS2 QoS,
- multi-publisher topics.

## Git quality bar

`main` should remain buildable and testable after each accepted phase.
Keep changes phase-scoped and avoid generating large numbers of speculative
placeholder files.
