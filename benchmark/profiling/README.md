# Profiling Evidence

This directory contains compact evidence for the profiling and optimization work. It explains the original results without
committing raw traces or per-run benchmark trees.

## Method

The baseline profile used clean Git revision
`4caf333234c644fd7405db4fb93a9039939587ad`. The optimized full matrix used clean revision
`971129a4495fcd870efe05b51d2dfe8e0087a0a9`. Both custom builds were Release binaries compiled
with `-O3 -DNDEBUG -g -fno-omit-frame-pointer`.

`perf` and `strace` were not installed on the measured WSL2 host. No perf counters, symbol
hotspots, syscall counts, or syscall-time percentages are claimed. The fallback observer records
measurement-boundary CPU ticks, faults, and context switches from `/proc`, plus 100 ms `wchan`
samples for the exact publisher, subscribers, and registry started by the existing benchmark
runner.

The representative matrix covers custom UDS, SHM Copy, and SHM Loan in latency and throughput
profiles at 64 B, 4 KiB, 64 KiB, 1 MiB, and 4 MiB where applicable, including 64 KiB and 4 MiB
1-to-4 fanout. Direct ROS2 context covers 64 KiB and 4 MiB 1-to-1. Before/after validation uses
three repetitions for both SHM paths at 64 B, 64 KiB, 1 MiB, and 4 MiB 1-to-1 plus 64 KiB and
4 MiB 1-to-4.

## Files

- `phase8_1_summary.json`: tool state, revisions, 24 focused before/after groups, validation, and
  optimization provenance.
- `before_after_summary.csv`: flattened focused metrics for inspection.
- `hotspot_summary.csv`: all observed process counters and top sampled wait channels. The name
  describes the investigation target; these rows are not symbol-level CPU profiles.
- `syscall_summary.csv`: exact tool availability and the limited syscall-boundary evidence that
  can be stated without `strace`.
- `run_phase8_1_profile.py`: the bounded observer used to collect the representative raw profile.

The complete optimized 441-run aggregate is in `benchmark/results/phase8_1_reference/`. Historical
pre-optimization results remain in `benchmark/results/phase8_reference/`.

## Reproduce

Build custom and direct ROS2 Release endpoints under a dedicated work tree, then source the ROS2
environment and run:

```bash
source /opt/ros/jazzy/setup.bash
source .work/public/profile/ros2/install/setup.bash
python3 benchmark/profiling/run_phase8_1_profile.py \
  --output-root .work/public/profile \
  --build-dir .work/public/profile/build \
  --ros-install .work/public/profile/ros2/install
```

The output root must not already exist. The observer uses the timings and queue settings from
`benchmark/configs/full.json`, one profiling repetition per case, and fails if a run is invalid or
the measured child-process set is not exact. Install and use `perf` or `strace` only with project
owner approval; do not relabel this fallback evidence as either tool's output.
