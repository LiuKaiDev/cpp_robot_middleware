# Development Workflow

## Repository Rules

Keep the core independent of ROS2, preserve the control/data-plane split, and require benchmark or
profiling evidence before transport optimization. Do not claim repository-wide zero-copy.

Use `.work/` for build, install, log, sanitizer, demo, and raw result output. Permanent
source, tests, documentation, reports, and compact committed reference evidence stay in their
existing repository directories.

## Core Build And Test

```bash
cmake -S . -B .work/local/build_debug -DCMAKE_BUILD_TYPE=Debug
cmake --build .work/local/build_debug -j
ctest --test-dir .work/local/build_debug --output-on-failure
```

Project code builds with `-Wall -Wextra -Wpedantic`. Run separate configurations with
`-DENABLE_ASAN=ON` and `-DENABLE_UBSAN=ON` when changing ownership, parsing, queues, or IPC.

## Install Contract

```bash
cmake -S . -B .work/local/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build .work/local/build_release -j
cmake --install .work/local/build_release --prefix .work/local/install
cmake -S examples/external_consumer -B .work/local/external_consumer \
  -DCMAKE_PREFIX_PATH="$PWD/.work/local/install"
cmake --build .work/local/external_consumer -j
.work/local/external_consumer/mw_external_consumer
```

Downstream projects consume `find_package(mw CONFIG REQUIRED)` and `mw::mw_core`.

## Demos And Documentation

Run the bounded non-ROS scenarios and local Markdown-link validation with:

```bash
MW_BUILD_DIR="$PWD/.work/local/build_release" scripts/demo/run_all_smoke.sh
python3 scripts/validate_markdown_links.py
```

The ROS2 adapter is a separate ament package and must be built against the installed core. See
[ROS2_ADAPTER.md](ROS2_ADAPTER.md). Benchmark commands and evidence boundaries are in
[BENCHMARK.md](BENCHMARK.md).

## Change Discipline

Keep commits purpose-oriented, preserve unrelated worktree changes, and run checks in proportion
to the ownership and process boundaries touched. Do not commit `.work/`, raw benchmark matrices,
coverage output, profiler output, build trees, or generated ROS logs. Do not create tags or push
unless the repository maintainer explicitly requests it.
