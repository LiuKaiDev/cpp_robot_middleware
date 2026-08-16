# Phase 9 Report

## Scope

Phase 9 finalized the project entry point, architecture/protocol/memory/lifecycle documentation,
reproducible demos, portfolio/interview material, and final evidence. The only feature closure was
a bounded read-only registry statistics query and `mwctl stats`, using existing counters. No data
plane, benchmark hot path, ROS2 core dependency, optional optimization, or Phase 10 work was added.

## Files Added

- `CODEX_TASKS/PHASE_9.md`
- `docs/ARCHITECTURE.md`, `PROTOCOL.md`, `MEMORY_MODEL.md`, `MESSAGE_LIFECYCLE.md`
- `docs/DEMO.md`, `KNOWN_LIMITATIONS.md`, `PROJECT_COMPLETION_CHECKLIST.md`
- `docs/INTERVIEW_GUIDE.md`, `RESUME.md`, and this report
- `docs/assets/demo/terminal_demo.txt` and `docs/assets/demo/demo.gif`
- `scripts/validate_markdown_links.py`
- `scripts/demo/` common helpers, seven scenario scripts, smoke runner, compact benchmark config,
  reference summarizer, and transcript renderer

## Files Modified

- Final entry and repository guidance: `README.md`, `START_HERE.md`, `GITHUB_REPO_INFO.md`,
  `docs/DEVELOPMENT_WORKFLOW.md`
- Technical consistency: `docs/BENCHMARK.md`, `CONTROL_PLANE.md`, `DATA_PLANE.md`,
  `FAILURE_MODEL.md`, `MEMORY_POOL.md`, `QUEUES_AND_LOANING.md`, `ROS2_ADAPTER.md`, and
  `UDS_BASELINE.md`
- Statistics protocol/client/state/server/CLI:
  `middleware/src/detail/control_protocol.*`, `registry_client.*`,
  `registry/include/mw/registry/registry_state.hpp`, `registry/src/registry_state.cpp`,
  `registry/src/registry_server.cpp`, and `tools/mwctl/main.cpp`
- Existing protocol, liveness, and registry discovery tests

## Files Removed

`MANIFEST.sha256.md` was the original bootstrap-package hash list. Its hashes no longer described
the repository and it had no build, release, or provenance role.

## Repository Cleanup

Eight ignored, untracked raw/development benchmark result directories were audited and removed:
the 37 MiB short acceptance run, 106 MiB full raw run, one backpressure check, and five smoke/dev
runs. Git tracked zero files in those paths. The 18 tracked files in
`benchmark/results/phase8_reference/` and `phase8_1_reference/` were preserved. Phase-local build,
install, ROS, smoke, log, and raw output was contained under `.work/phase_9/` and removed after this
report and permanent demo evidence were produced.

## Full Mandatory Feature Audit

PASS, 29/29 mandatory PROJECT_PLAN items. The evidence matrix is in
[`PROJECT_COMPLETION_CHECKLIST.md`](../PROJECT_COMPLETION_CHECKLIST.md). Optional ideas such as
lock-free queues, `eventfd`, multi-publisher topics, remote transport, custom RMW, persistence, and
security remain explicitly not implemented.

## README Finalization

PASS. The README now presents the actual project, architecture, public API, ownership/lifecycle,
backpressure/failure behavior, build/install/ROS2/demo commands, latest optimized benchmark
reference, measured strengths and costs, documentation navigation, limitations, future boundary,
and unselected license status. It does not claim full DDS, hard real time, production readiness, or
repository-wide zero-copy.

## Architecture, Protocol, Memory, And Lifecycle

- Architecture documentation: PASS, including a Mermaid diagram, component boundaries, dependency
  direction, process/thread model, resource ownership, and error boundaries.
- Protocol documentation: PASS, including control protocol v5, `QUERY_STATS`, UDS payload frames,
  SHM wakes/releases, validation, heartbeat, and recovery messages.
- Memory model: PASS, including segment layout, local ABI assumptions, publish/consume ordering,
  logical handle identity, and copy boundaries.
- Message lifecycle: PASS, including FREE/LOANED/PUBLISHED/RELEASED transitions, guard and endpoint
  references, `LoanedSample`, `SampleView`, fanout, backpressure, and crash repair.
- Failure model: PASS, updated for the bounded 1 ms discovery reuse and accessible registry stats.

## ROS2 Adapter

PASS. The final documentation preserves the dependency direction, supported String/Twist/Image
types, CDR serialization boundary, copy costs, parameters, build/test workflow, and limitations.
The core was installed first and the independent adapter consumed `mw::mw_core`.

## Benchmark Finalization And Phase 8.1 Results

The primary evidence remains `benchmark/results/phase8_1_reference/` at Git `971129a`: 441/441
valid runs and 147/147 valid groups on WSL2, Intel i5-8300H, 8 logical CPUs, GCC 13.3, Release
`-O3 -DNDEBUG -g -fno-omit-frame-pointer`, ROS2 Jazzy, and `rmw_fastrtps_cpp`.

Representative 1-to-1 medians pair fixed-rate p50 with maximum-rate correct throughput:

| Size | UDS p50 us / MiB/s | SHM Copy p50 us / MiB/s | SHM Loan p50 us / MiB/s | ROS2 p50 us / MiB/s |
| ---: | ---: | ---: | ---: | ---: |
| 64 B | 80.7 / 9.0 | 170.0 / 8.0 | 157.2 / 9.2 | 154.1 / 1.4 |
| 64 KiB | 97.6 / 1104.6 | 254.0 / 1070.5 | 240.4 / 1063.9 | 184.5 / 529.4 |
| 1 MiB | 742.7 / 1167.9 | 407.8 / 1405.5 | 287.7 / 1474.5 | 11205.7 / 920.1 |
| 4 MiB | 2053.4 / 1113.0 | 626.9 / 1500.0 | 274.1 / 1524.6 | 11945.9 / 947.0 |

UDS retained the lowest p50 through 64 KiB on this host. SHM crossed over at larger payloads; at
4 MiB Loan delivered 1524.6 MiB/s versus 1113.0 MiB/s UDS, but total publisher/subscriber peak RSS
was about 92.8 MiB SHM versus 16.3 MiB UDS. At 4 MiB 1-to-4, aggregate delivery was 2562.6 MiB/s
UDS, 4146.6 Copy, and 4224.7 Loan. These are host-specific measurements, not universal rankings.

## Demo Scripts And Smoke Results

All final scripts use exact child PIDs, unique names, bounded waits, EXIT cleanup, and no broad
process or SHM deletion. The ROS2 script starts the two installed bridge executables directly
rather than owning `ros2 run` wrapper PIDs.

| Demo | Result | Verified behavior |
| --- | --- | --- |
| Basic Pub/Sub | PASS | 5/5 64-byte SHM messages and live `mwctl` output |
| Large Message | PASS | 3/3 exact 4 MiB SHM Copy messages |
| Multi Subscriber | PASS | four processes reported the same logical chunk tuple |
| Backpressure | PASS | 4/4 valid short runs covering all three policies |
| Crash Recovery | PASS | exact subscriber PID exited 137, registry removed it, replacement transferred |
| ROS2 Adapter | PASS | actual String traversed ROS2 -> SHM -> ROS2 on distinct topics |
| Benchmark | PASS | parsed the committed fully valid optimized aggregate and charts |

`scripts/demo/run_all_smoke.sh --with-ros2` completed with `DEMO_SMOKE_RESULT PASS`. The ROS script
waits for actual middleware endpoint counts and ROS output subscription matching rather than a
fixed discovery delay. A post-run process, `/dev/shm`, and `/tmp` audit found no bridge or demo
resource residue.

## Terminal Transcript And GIF

PASS. `docs/assets/demo/terminal_demo.txt` is the plain output of an actual successful basic demo,
including 5/5 correctness and `mwctl stats`. Pillow rendered it to a 960x540 animated GIF; the
asset was opened and visually checked. No output was invented.

## Interview And Resume Guides

PASS. The interview guide covers the actual architecture, ownership, failure, backpressure, ROS2,
and measurement tradeoffs. The Chinese/English resume guide uses only values from the optimized
committed reference and preserves WSL2/environment context.

## Runtime Metrics Audit

PASS. Existing `PublishResult`, queue/pool counters, and benchmark artifacts remain the detailed
data-plane sources. The new control opcode returns current node/topic/publisher/subscriber counts
and lifetime heartbeat-receive, suspected-transition, and dead-node counters. `mwctl stats` is a
read-only client of that bounded snapshot; it is not presented as a general telemetry system.
Malformed/trailing response data is rejected, request bodies must be empty, and the existing unit
and real-registry CLI integration coverage was extended.

## Build And Test Results

| Check | Result |
| --- | --- |
| Clean Debug build | PASS |
| Debug CTest | PASS, 83/83 |
| ASan (`detect_leaks=1:halt_on_error=1`) | PASS, 83/83 |
| UBSan (`halt_on_error=1:print_stacktrace=1`) | PASS, 83/83 |
| Clean Release build and CTest | PASS, 83/83 |
| Install/export | PASS, library, headers, executables, and CMake package installed |
| External consumer | PASS, `find_package(mw)` / `mw::mw_core`, printed `0.1.0` |
| ROS2 adapter regression | PASS, 24/24 |
| Benchmark smoke | PASS, 16/16 valid across four transports |

## Documentation And Command Verification

The local validator passed after the report and demo assets were present. Shell scripts passed
`bash -n`; Python tools parsed and ran. Important README/demo/ROS2/benchmark commands were mapped to
real binaries or scripts. The build, install, external consumer, demos, ROS2 build/test, ROS2 bridge,
benchmark runner, and benchmark summary commands were executed. CLI usage was also inspected for
`mw_registryd`, the ping examples, and `mwctl`; those binaries do not claim a conventional
successful `--help` interface.

## Core / ROS2 Dependency Audit

PASS. Source search found no `rclcpp`, `rmw_`, or `ament_` references under the core, registry,
tools, examples, root CMake, or CMake helpers. `ldd`/`readelf -d` on the installed `libmw_core.so`
showed only `libstdc++`, `libgcc_s`, and `libc` as needed libraries.

## Root Cleanliness

PASS after final cleanup. The expected source directories and root project files remain. There are
no root `build_*`, `install_*`, or `log_*` directories, no `.work` artifacts in Git, no ROS logs,
and no ignored old full raw benchmark matrices. The two compact committed references remain.

## Known Limitations And Future Work

The final limitation document records Linux/single-host scope, one publisher per topic, volatile
delivery, no security/persistence/distribution/hard-real-time guarantee, exact local pthread/atomic
ABI assumptions, registry restart boundary, recovery data loss, ROS2 serialization and three-type
limit, WSL2 benchmark environment, and unavailable perf/strace attribution.

Native-Linux profiling, `eventfd`, evidence-driven queue/allocator work, PointCloud2, remote
transport, multi-publisher semantics, CI, and registry restart recovery are listed only as NOT
IMPLEMENTED future work.

## License Status

No project license was selected and no `LICENSE` file was created.

## Final Project Status

COMPLETE for the planned Phase 0 through Phase 9 scope. Phase 9 is the final planned phase; no
additional phase was implemented.
