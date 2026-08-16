# Phase 8.1 Report

## Scope

Phase 8.1 completed the ordered repository-hygiene and profiling tasks. It profiled the existing
Phase 8 implementation before changing measured core code, identified a synchronous SHM discovery
bottleneck, implemented two local changes, validated them with focused before/after measurements,
and reran the complete Phase 8 matrix. The middleware remains Linux-local, C++17, single-publisher
per topic, and independent of ROS2 in the core.

No lock-free queue, allocator, eventfd, affinity, scheduler policy, transport protocol, or ROS2
implementation change was introduced.

## Repository Hygiene

Preflight started from clean `main` at
`d3a30b7280c60353675c4528e9b5e76c3a5bf99c`, equal to `origin/main`. Each root build, install, and
log candidate was checked with `git status --short -- <path>` and `git ls-files -- <path>` before
removal. None contained tracked files. About 600 MiB of reproducible generated output was removed.

The hygiene changes are commit `4caf333234c644fd7405db4fb93a9039939587ad`,
`chore: organize repository artifacts and work directories`.

## Root Before / After

Before cleanup, the root contained generated CMake, colcon, install, and log trees alongside source.
After cleanup, the root contains project source/metadata plus `.work` only while a phase is active.
The final Phase 8.1 cleanup removes `.work/phase_8_1`.

Permanent root directories are:

```text
benchmark  cmake  CODEX_TASKS  docs  examples
middleware  registry  ros2_adapter  tests  tools
```

No root `PHASE_*_REPORT.md`, `build_*`, `build-*`, `install_*`, `install-*`, `log_*`, or `log-*`
path remains.

## Reports Moved

`PHASE_0_REPORT.md` through `PHASE_8_REPORT.md` were moved with `git mv` to `docs/reports/`. Their
contents and history were preserved. Current README navigation points to the new location; historical
task instructions were not rewritten.

## Temporary Directories Removed

The following confirmed untracked generated paths were removed:

```text
_install
build_external_phase8
build_phase8_asan
build_phase8_compile
build_phase8_debug
build_phase8_ubsan
build_release
build_ros2_benchmark
build_ros2_phase8
install_ros2_benchmark
install_ros2_phase8
log
log_ros2_benchmark
log_ros2_phase8
log_ros2_phase8_final
log_ros2_phase8_final_result
log_ros2_phase8_final_test
log_ros2_phase8_test
```

No benchmark source, test, documentation, committed reference, or historical report was deleted.

## New .work Convention

`.gitignore` and `docs/DEVELOPMENT_WORKFLOW.md` establish `.work/phase_X/` for phase-local build,
install, log, sanitizer, profiling, and raw benchmark output. Compact benchmark/profiling evidence,
source, tests, tasks, and reports remain permanent.

## Git Revision Profiled

- Original Phase 8 implementation: `cf353091f7149ac4f458ec77d98f23bb948155b2`.
- Clean Phase 8.1 baseline after documentation-only hygiene:
  `4caf333234c644fd7405db4fb93a9039939587ad`.
- Optimized source and clean full-matrix revision:
  `971129a4495fcd870efe05b51d2dfe8e0087a0a9`.

The baseline profile manifest records clean `4caf333`. The after profile was collected from the
three-file optimized source diff before it was committed; that exact source became `971129a`. The
complete optimized matrix records clean `971129a` with `git_dirty: false`.

## Build Configuration

The profiling builds used GCC 13.3.0, C++17, `CMAKE_BUILD_TYPE=Release`, and:

```text
-O3 -DNDEBUG -g -fno-omit-frame-pointer
```

ASan/UBSan and Debug binaries were never used for performance data. The direct ROS2 benchmark used
ROS2 Jazzy and the normal `rmw_fastrtps_cpp` implementation. The benchmark runner retained the
Phase 8 queue depth, pool classes, payloads, process topology, timing, ordering seed, and validity
checks.

## Machine Information

| Field | Value |
| --- | --- |
| OS | Linux, WSL2 |
| Kernel | `6.18.33.2-microsoft-standard-WSL2` |
| Architecture | x86_64 |
| CPU | Intel Core i5-8300H, 8 logical CPUs |
| Memory | 8,260,288,512 bytes |
| Compiler | GCC 13.3.0 |
| CMake | 3.28.3 |
| Python | 3.12.3 |
| ROS | Jazzy, `rmw_fastrtps_cpp` |

No affinity, CPU isolation, priority, frequency control, or background-load suppression was used.

## perf Availability

`command -v perf` returned no executable. `perf --version` could not run. No perf stat counters or
perf record/report symbols are present, and none are fabricated in the compact evidence.

## strace Availability

`command -v strace` returned no executable. `strace --version` could not run. Consequently this
report does not claim syscall counts or syscall-time percentages.

## perf Restrictions

The host reported:

```text
/proc/sys/kernel/perf_event_paranoid = 2
/proc/sys/kernel/kptr_restrict = 1
```

No package installation, sudo command, sysctl, or kernel setting change was attempted.

## Profiling Methodology

The existing Phase 8 runner drove every profile. A bounded observer identified only exact runner
descendants matching the publisher, subscriber, and registry executables. At the acknowledged
measurement start/end it sampled `/proc/<pid>/stat` and `/proc/<pid>/status` for CPU ticks, minor and
major faults, and voluntary/nonvoluntary context switches. During the five-second window it sampled
`/proc/<pid>/wchan` every 100 ms.

This fallback is process-boundary evidence. A wait-channel sample indicates where a sleeping task
was observed; it is not a stack profile, syscall count, or proof of where CPU time was spent. Raw
profiles remained under `.work/phase_8_1`; compact rows are in `benchmark/profiling/`.

Each representative profile case used one repetition for diagnosis. The separate before/after
matrix used three repetitions per group. The complete acceptance matrix used the normal three
repetitions.

## Representative Case Matrix

| Purpose | Sizes/topology | Custom transports | Profiles |
| --- | --- | --- | --- |
| Small fixed cost | 64 B and 4 KiB, 1-to-1 | UDS, SHM Copy, SHM Loan | latency, throughput |
| Crossover | 64 KiB, 1-to-1 | UDS, SHM Copy, SHM Loan | latency, throughput |
| Large | 1 MiB and 4 MiB, 1-to-1 | UDS, SHM Copy, SHM Loan | latency, throughput |
| Fanout | 64 KiB and 4 MiB, 1-to-4 | UDS, SHM Copy, SHM Loan | latency, throughput |
| External context | 64 KiB and 4 MiB, 1-to-1 | direct ROS2 | latency, throughput |

The before and after representative profiles were both 46/46 valid.

## Baseline Benchmark References

- Historical compact Phase 8 aggregate: `benchmark/results/phase8_reference/`.
- Historical full raw Phase 8 tree: `benchmark/results/phase8_full_cf35309/`, preserved locally,
  106 MiB.
- Phase 8.1 focused baseline: clean `4caf333`, same Release flags as after.
- Optimized compact full matrix: `benchmark/results/phase8_1_reference/`.

The historical compact and raw results were not overwritten. Because the 106 MiB raw Phase 8 tree
is reproducible and its compact aggregate is committed, the owner may delete it to reclaim space if
per-run logs/CSV are no longer needed. It was deliberately not deleted by this task.

## CPU Hotspots

Symbol-level CPU hotspots are unavailable because perf was not installed. Process-level CPU tick
evidence still shows the changing work distribution:

| Baseline throughput case | Publisher ticks | Subscriber ticks | Registry ticks | Messages/s |
| --- | ---: | ---: | ---: | ---: |
| UDS 64 B, 1-to-1 | 219 | 497 | 0 | 142,361 |
| SHM Copy 64 B, 1-to-1 | 284 | 115 | 193 | 8,759 |
| SHM Loan 64 B, 1-to-1 | 290 | 98 | 175 | 9,178 |
| UDS 64 KiB, 1-to-1 | 256 | 496 | 0 | 17,526 |
| SHM Copy 64 KiB, 1-to-1 | 315 | 312 | 158 | 7,296 |
| SHM Loan 64 KiB, 1-to-1 | 327 | 247 | 158 | 8,468 |

The UDS subscriber and optimized SHM subscriber frequently consumed nearly a full core in
throughput cases. Large-message SHM throughput also drove the subscriber near one core while the
publisher alternated between running and queue waits. Function attribution is intentionally left
open.

## Syscall Analysis

No dynamic syscall ranking is available without strace. Source inspection establishes only these
boundaries:

- UDS sends a fixed header and payload through the data socket and receives into an owning vector.
- SHM sends fixed metadata wakes/releases through UDS while payload bytes remain in mapped memory.
- registry resolution is a synchronous control-socket request/response.
- pool and queue `shm_open`, `ftruncate`, `mmap`, `munmap`, and `shm_unlink` calls are in setup,
  mapping-lifetime, and teardown code, not in `dispatchPublished`.

The baseline UDS publisher was repeatedly sampled in `sock_alloc_send_pskb`; the registry was
mostly in `do_epoll_wait`. These are wait observations, not syscall totals. Dynamic confirmation
that SHM performs no per-message mapping syscall remains a strace limitation; Phase 4 lifecycle
tests and the source structure continue to cover the design claim.

## Context Switch Analysis

The baseline relationship was unusually direct: small-message SHM publisher and registry voluntary
context switches were approximately one per publication, while UDS resolved once and did not show
the same registry scaling.

| Throughput case | Stage | Publisher VCS/publish | Registry VCS/publish | Messages/s |
| --- | --- | ---: | ---: | ---: |
| SHM Copy 64 B | before | 0.997 | 1.000 | 8,759 |
| SHM Copy 64 B | after | 0.185 | 0.007 | 132,772 |
| SHM Loan 64 B | before | 0.995 | 0.999 | 9,178 |
| SHM Loan 64 B | after | 0.016 | 0.006 | 151,129 |
| SHM Copy 64 KiB | before | 1.025 | 1.003 | 7,296 |
| SHM Copy 64 KiB | after | 0.622 | 0.050 | 17,342 |
| SHM Loan 64 KiB | before | 0.999 | 1.000 | 8,468 |
| SHM Loan 64 KiB | after | 0.641 | 0.050 | 17,302 |

At 4 MiB, throughput is below 1,000 messages/s, so the one-millisecond refresh expires between most
messages and registry VCS remains approximately one per publication. The optimization therefore
targets the measured high-rate fixed control cost rather than claiming a universal improvement.

UDS 1-to-4 subscriber VCS was high (96,717 at 64 KiB and 52,025 at 4 MiB across four subscribers),
consistent with independent receiving processes and socket delivery. This is reported as
correlation, not sole causation.

## Allocation Analysis

No allocator symbols could be ranked. Across 21 custom throughput profile cases, major faults were
zero before and after; the maximum minor-fault delta for any measured process was two. This rules
out page-fault pressure in those measurement windows, but does not prove that `malloc`/`free` are
free or absent.

Source inspection shows bounded pool/queue storage and persistent mappings. It also shows temporary
discovery containers in each resolve. Because allocator cost was not directly profiled, no custom
allocator or cache was introduced.

## Copy Analysis

| Transport | Application generation | Full application-to-middleware payload copy | Kernel boundary | Subscriber API used by benchmark |
| --- | --- | --- | --- | --- |
| UDS | Fill reusable application vector | No extra owning middleware buffer observed in publisher source | Header and payload socket writes; kernel internals not claimed | Owning receive vector |
| SHM Copy | Fill reusable application vector | One explicit `memcpy` into a pool chunk | Fixed wake/release metadata only | Direct `SampleView` |
| SHM Loan | Fill `LoanedSample::data()` in the pool chunk | None between loan fill and `publishLoaned` | Fixed wake/release metadata only | Direct `SampleView` |

The loan-to-view path is copy-free for middleware payload transfer. This is not a whole-system or
kernel zero-copy claim. No memcpy hotspot percentage is claimed without perf.

## Blocking / Futex Analysis

Baseline large SHM throughput publishers were repeatedly sampled in `futex_do_wait`: 11/50 samples
for Copy 1 MiB, 14/50 for Loan 1 MiB, 19/50 for Copy 4 MiB, and 26/50 for Loan 4 MiB. These cases
also reported thousands of blocked queue operations and 1.1 to 2.6 seconds of aggregate blocked
time in a five-second window. This supports the documented `BLOCK_WITH_TIMEOUT` condition-variable
path and subscriber consumption rate as the observed wait mechanism. Pool allocation failures were
zero.

After control-plane throttling, 64 B and 64 KiB SHM reached the queue/subscriber limit instead of
the registry limit, so blocked counts and sampled throughput latency increased while delivered
throughput rose. Backpressure policy, queue depth, and timeout were not changed. The optimized full
backpressure experiment remained valid for all nine runs and retained distinct drop-newest,
drop-oldest, and block-with-timeout outcomes.

## UDS Findings

UDS was competitive for small and medium payloads because an established connection did not make a
synchronous registry resolve for each message. In the 64 B baseline it delivered 142,361 messages/s
while the small SHM paths plateaued near 9,000 messages/s. The UDS publisher was often observed in
socket send wait and the subscriber used close to one core; no symbol or syscall-time split is
available.

With size growth, payload socket transfer became increasingly significant: UDS 4 MiB 1-to-1
delivered 1,104 MiB/s in the baseline profile and the publisher was sampled in
`sock_alloc_send_pskb` 21/50 times. The source confirms a user/kernel payload boundary but this
report does not infer a specific kernel copy count.

## SHM Copy Findings

SHM Copy paid the same per-message synchronous resolve as Loan plus one explicit payload `memcpy`
into the pool. Before optimization, the fixed resolve dominated 64 B through 4 KiB and remained
important at 64 KiB. At 1 MiB and 4 MiB the fixed control cost was amortized and the explicit copy
plus queue/subscriber throughput became more visible.

The 1 ms discovery refresh removes most high-rate resolves but deliberately does not alter the copy,
pool, reference, queue, or backpressure semantics.

## SHM Loan Findings

SHM Loan shared the same control-plane bottleneck at small sizes. Its application writes directly
into an allocated chunk and the subscriber reads the same logical chunk through `SampleView`, so it
avoids the Copy path's full payload `memcpy`. That difference is small while fixed control metadata
dominates, but explains the stronger Phase 8 fixed-rate latency at 1 MiB and especially 4 MiB.

Removing the high-rate resolve exposed accumulation of empty-to-nonempty wake frames. A 64 B Loan
smoke reproducibly blocked the publisher in `sock_alloc_send_pskb` while the subscriber waited on
the shared queue mutex held by that send. The accepted fix nonblockingly drains redundant complete
wake frames once the pool is installed. A 5,000-cycle queue-empty regression test covers this
failure mode.

## ROS2 Context

ROS2 remained an unmodified external baseline. The representative profile was 4/4 valid. At 64 KiB
throughput it delivered 545.6 MiB/s in the baseline profile; publisher/subscriber CPU ticks were
567/593 and the subscriber had 28,165 voluntary context switches. At 4 MiB it delivered 830.3
MiB/s with 4,606 subscriber voluntary context switches. Fixed-rate 4 MiB latency was much higher
than the custom SHM paths in this measured configuration.

Without perf/strace, this report does not attribute ROS2 costs to rclcpp, serialization, DDS, or
Fast DDS internals. No ROS2 code or configuration was optimized.

## Small Message Findings

The strongest observed small-message contributor was the SHM control-plane round trip, not payload
copy size. Baseline Copy/Loan registry VCS tracked publication count at 64 B and 4 KiB, and both
paths plateaued near 10,000 messages/s despite a 64x payload-size change. UDS did not have that
per-message registry behavior.

In the clean optimized full matrix, 64 B 1-to-1 throughput reached 130,792 messages/s for Copy and
151,393 for Loan, compared with 148,003 for UDS. At 4 KiB it reached 76,412 and 96,931 messages/s,
compared with 95,342 for UDS. These numbers are session-specific results, not a general transport
ranking.

## Large Message Findings

At 1 MiB and 4 MiB, the one-millisecond cache usually expires between publications, so control-plane
resolve reduction is limited and focused before/after throughput stayed within one percent. The
existing architecture still explains the scaling difference: UDS crosses a payload socket
boundary, Copy writes one full chunk copy, and Loan writes directly into the shared chunk.

Focused fixed-rate 4 MiB p50 changed by -3.7% Copy and -1.2% Loan; p99 changed by -10.6% and
-12.9%. Full-session large throughput differs by several percent from historical Phase 8, within
the documented cross-session scheduling variability and not attributed to the optimization.

## 1->4 Fanout Findings

Fanout enqueues one logical chunk handle into independent bounded subscriber queues. Baseline 64 KiB
1-to-4 SHM registry VCS was still about one per publication. After optimization it was about 0.08
per publication and focused publisher throughput rose 132.8% Copy and 121.9% Loan.

At 4 MiB 1-to-4, focused throughput changed only +0.4% Copy and +0.3% Loan, consistent with the
refresh expiring between large publications and subscribers saturating independent cores. Payload,
sequence, allocation, and resource validation remained clean.

## Identified Bottlenecks

1. **High confidence:** SHM called synchronous `RESOLVE_ENDPOINT` for every publication even with
   established connections. Publication-count-correlated publisher/registry VCS, the small-message
   plateau, UDS contrast, and source path all support this conclusion.
2. **High confidence after the first change:** redundant wake frames could accumulate when the
   shared queue repeatedly returned to empty. A reproducible hang plus exact publisher/subscriber
   wait channels and the lock/send ordering identified the mechanism.
3. **High confidence:** large SHM throughput is constrained by bounded queue/subscriber consumption,
   demonstrated by recorded blocked metrics and publisher futex wait samples. This is intended
   backpressure, not removed by the optimization.
4. **Source-backed, not symbol-profiled:** SHM Copy has one full chunk copy while Loan does not.

## Optimization Decision

**A. OPTIMIZATION JUSTIFIED.** Confidence: HIGH.

The fixed SHM control request was directly correlated with publications and had a local,
explainable remedy. The wake-drain change was required to preserve progress after removing that
artificial rate limit.

## Optimization Implemented

1. An SHM publisher with established connections reuses the most recent compatible discovery for
   up to 1 ms. First discovery, no connections, peer events, disconnects, and dispatch/queue
   failures force immediate discovery. A connected publisher discovers an additional subscriber on
   the next bounded refresh.
2. An SHM subscriber with an installed pool drains redundant complete wake notifications using
   nonblocking receives before consuming the shared queue. The queue remains the source of truth;
   frame validation and disconnect behavior are unchanged.

The implementation is commit `971129a4495fcd870efe05b51d2dfe8e0087a0a9`,
`perf: reduce measured SHM control-plane overhead`.

## Before / After Results

All 24 focused groups used identical Release flags, machine, configuration, duration, topology, and
three repetitions. All 72 before and 72 after runs were valid.

Maximum-rate publisher messages/s change:

| Case | SHM Copy | SHM Loan |
| --- | ---: | ---: |
| 64 B, 1-to-1 | +1138.4% | +1389.0% |
| 64 KiB, 1-to-1 | +111.7% | +102.3% |
| 1 MiB, 1-to-1 | +0.2% | -0.2% |
| 4 MiB, 1-to-1 | +0.9% | +0.3% |
| 64 KiB, 1-to-4 | +132.8% | +121.9% |
| 4 MiB, 1-to-4 | +0.4% | +0.3% |

Fixed-rate latency p50/p99 change (negative is lower):

| Case | Copy p50 / p99 | Loan p50 / p99 |
| --- | ---: | ---: |
| 64 B, 1-to-1 | -24.2% / +1.8% | -22.8% / +10.6% |
| 64 KiB, 1-to-1 | -1.4% / -0.1% | +2.2% / -2.5% |
| 1 MiB, 1-to-1 | +1.9% / -11.7% | -1.8% / -9.1% |
| 4 MiB, 1-to-1 | -3.7% / -10.6% | -1.2% / -12.9% |
| 64 KiB, 1-to-4 | +4.5% / -1.8% | +1.4% / +4.3% |
| 4 MiB, 1-to-4 | +0.2% / +12.8% | +0.6% / +19.1% |

The 4 MiB latency profile has only 50 messages per repetition and Phase 8 documented large p99
run-to-run spans. No corresponding p50 or correctness regression appears. Throughput-profile queue
latency at 64 KiB rose because the offered/delivered rate approximately doubled and saturated the
same depth-8 bounded queue; it is not compared as fixed-load latency.

## Correctness Regression

Release Core CTest: PASS, 83/83. This includes new tests for bounded discovery refresh and 5,000
empty-queue wake cycles, plus all lifecycle, backpressure, fanout, and crash-recovery tests.

## ASan

PASS, 83/83 with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`. No diagnostics.

## UBSan

PASS, 83/83 with `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. No diagnostics.

## ROS2 Adapter Regression

PASS. The installed optimized core was consumed by a clean Release colcon build. Colcon reported
24 tests, 0 errors, 0 failures, and 0 skipped.

## Phase 8 Benchmark Smoke

PASS, 16/16 across UDS, SHM Copy, SHM Loan, and direct ROS2 at 64 B and 64 KiB in latency and
throughput profiles.

## Full Matrix Rerun

PASS on clean optimized revision `971129a`:

- main matrix: 432/432 valid;
- backpressure experiment: 9/9 valid;
- aggregate groups: 147/147 valid;
- payload errors: 0;
- duplicate/out-of-order sequence errors: 0;
- custom main drops/queue overflows: 0;
- allocation failures: 0;
- invalid reasons: 0;
- minimum measurement duration: 5.000000659 seconds;
- wall time: approximately 62 minutes 15 seconds.

The original reference remains unchanged. The new compact sanitized reference is
`benchmark/results/phase8_1_reference/` and is about 1.2 MiB. The 106 MiB Phase 8.1 raw tree was not
committed.

## Known Limitations

- perf and strace were unavailable; CPU symbol and syscall-count questions remain partially open.
- `/proc` counters and 100 ms wait-channel samples are coarse and do not replace call graphs.
- WSL2 scheduling and background activity limit cross-session comparisons.
- Throughput latency is systematically sampled and changes load when throughput changes.
- Fixed-rate 4 MiB p99 uses only 50 messages per repetition.
- Kernel-internal socket copy counts are not claimed.
- No general allocator-hotspot conclusion is possible from page faults alone.
- The 1 ms discovery interval is internal and not a public QoS guarantee.

## Future Optimization Candidates

- Repeat the same representative matrix with perf and strace if the owner approves installing them.
- Profile release/refcount and queue critical sections with call graphs before considering any lock
  redesign.
- Evaluate a generation/event-driven discovery refresh only if the remaining low-rate registry work
  becomes measurable and the discovery contract is kept explicit.
- Investigate a structured nonblocking notification mechanism only with evidence; eventfd remains
  deferred.
- Measure allocator symbols before considering any allocation cache or custom allocator.

No future candidate is implemented in this phase.

## Temporary Artifact Cleanup

PASS. After compact profiling evidence, the sanitized optimized reference, task file, and report
were written, `.work/phase_8_1` was removed. No new raw perf data, trace tree, build, install, log,
or 106 MiB optimized benchmark tree remains in the repository work area. The original local
`benchmark/results/phase8_full_cf35309` was preserved as explicitly required.

## Phase Boundary

Phase 8.1 stops after profiling, the two accepted local optimizations, regression, full matrix,
documentation, and cleanup.

Phase 9 was not implemented.
