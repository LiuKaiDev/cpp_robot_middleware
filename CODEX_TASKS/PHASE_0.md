# Codex Task — Phase 0: Project Skeleton / Library Packaging

## Role

You are implementing **Phase 0 only** of `cpp_robot_middleware`.

The authoritative architecture and boundaries are in `PROJECT_PLAN.md`.
Repository-wide implementation rules are in `AGENTS.md`.

Do not implement any Phase 1 Pub/Sub transport behavior.

## First action: inspect before editing

Before changing any file:

1. Read `PROJECT_PLAN.md`, especially:
   - project positioning and boundaries,
   - repository planning,
   - library packaging,
   - Phase 0,
   - v1 exclusions,
   - engineering quality rules,
   - Git/Codex implementation principles.
2. Read `AGENTS.md`.
3. Inspect the current repository.
4. Give a concise Phase 0 implementation plan.
5. List files you will add/modify and the responsibility of each.
6. Explain resource ownership / thread model / error handling for Phase 0.
   For this phase, explicitly say when an item is intentionally not applicable.

After that summary, continue implementation without waiting for approval unless
you discover a material conflict with `PROJECT_PLAN.md`.

## Phase 0 goal

Create a minimal, independent Linux C++17 project skeleton that proves:

- the project configures and builds with CMake;
- a shared library target is produced as `libmw_core.so`;
- public headers are exposed under `include/mw/` via the repository's
  `middleware/include/mw/` tree;
- tests are integrated with CTest;
- install/export packaging works;
- an external consumer can use the installed package;
- formatting and basic project documentation exist.

The implementation must be deliberately small. Do not create empty files for
future phases.

## Required repository shape for Phase 0

Create only what Phase 0 needs, approximately:

```text
cpp_robot_middleware/
├── AGENTS.md
├── PROJECT_PLAN.md
├── README.md
├── CMakeLists.txt
├── .gitignore
├── .clang-format
├── cmake/
│   └── mwConfig.cmake.in
├── middleware/
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── mw/
│   │       └── version.hpp
│   └── src/
│       └── version.cpp
├── tests/
│   ├── CMakeLists.txt
│   └── version_test.cpp
├── examples/
│   └── external_consumer/
│       ├── CMakeLists.txt
│       └── main.cpp
├── CODEX_TASKS/
│   └── PHASE_0.md
└── PHASE_0_REPORT.md
```

You may make a small adjustment if CMake packaging requires it, but do not
create Phase 1+ source files such as `context.hpp`, `publisher.hpp`,
`subscriber.hpp`, socket transports, shared-memory code, registry code, ROS2
code, benchmark code, or speculative docs.

## CMake / library contract

Use modern target-based CMake.

Required contract:

- project language: C++;
- C++ standard: C++17;
- no compiler extensions required;
- shared library target name: `mw_core`;
- installed artifact on Linux: `libmw_core.so`;
- exported CMake namespace: `mw::`;
- installed target should be consumable as `mw::mw_core`;
- public include path must make this valid:

```cpp
#include <mw/version.hpp>
```

- provide package config/version files so an external project can use:

```cmake
find_package(mw CONFIG REQUIRED)
target_link_libraries(example PRIVATE mw::mw_core)
```

- use `GNUInstallDirs` and standard CMake install/export mechanisms;
- project code should compile with at least:
  `-Wall -Wextra -Wpedantic` on GCC/Clang.

Do not add ROS2, Boost IPC, ZeroMQ, iceoryx, DDS, or other middleware
dependencies.

## Minimal public API for Phase 0

Do not invent future Pub/Sub APIs.

Use only a tiny version API sufficient to verify headers/library linkage, e.g.:

```cpp
namespace mw {
const char* version() noexcept;
}
```

A compile-time version constant is also acceptable if it remains small and
clear.

## Testing

Integrate CTest.

Preferred: GoogleTest if it is already available in the environment or can be
used without making the build fragile.

If GoogleTest is not available, do not silently add a large vendored
dependency or unrelated framework. Use a minimal CTest smoke test for Phase 0
and clearly record the limitation/decision in `PHASE_0_REPORT.md`.

At minimum verify:

1. the version API links and returns the expected value;
2. the shared library target builds;
3. `ctest --test-dir build --output-on-failure` passes.

## Install/export acceptance

The external consumer must be a separate CMake project that consumes the
**installed** package, not the in-tree target.

Acceptance flow should work with commands equivalent to:

```bash
rm -rf build _install build_external

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

cmake --install build --prefix "$PWD/_install"

cmake -S examples/external_consumer -B build_external   -DCMAKE_PREFIX_PATH="$PWD/_install"
cmake --build build_external -j
./build_external/mw_external_consumer
```

If generator/platform differences require a small command change, document the
exact commands you actually ran.

## README.md for Phase 0

Create only a skeleton README. It should include:

- Project Overview
- Why This Project
- Current Status: Phase 0
- Architecture Direction (brief; UDS control plane + SHM data plane)
- Build
- Test
- Install / external consumer
- Development Phases (0-9 short list)
- Known Limitations

Do not write benchmark results or claim features that are not implemented.

Never claim zero-copy in Phase 0.

## .gitignore

At minimum ignore common local/generated files such as:

- `build/`
- `build_*/`
- `_install/`
- `compile_commands.json`
- editor/OS noise that is clearly safe to ignore.

Do not ignore source, configuration, reports, or benchmark results globally in
ways that would hide future required artifacts.

## PHASE_0_REPORT.md

At the end, create a report containing:

1. Scope implemented
2. Files added/modified
3. Design decisions
4. Build commands run
5. Test commands run
6. Install/export verification
7. External consumer verification
8. Exact results
9. Warnings / limitations
10. Confirmation that Phase 1 was not implemented

## Final response to the user

When finished, report:

- what changed;
- build result;
- test result;
- install/export result;
- external consumer result;
- files created;
- any environment limitation;
- explicit statement: `Phase 1 was not implemented`.

Do not commit, push, merge, or tag unless the user explicitly asks.
