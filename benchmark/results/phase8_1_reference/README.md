# Phase 8.1 Optimized Reference Results

These compact artifacts were generated from the complete 441-run Phase 8 main and backpressure
matrix on Git commit `971129a4495fcd870efe05b51d2dfe8e0087a0a9`.

- `machine.json`: sanitized host, toolchain, Git, ROS, and effective configuration metadata.
- `config.json`: deterministic 441-case order and benchmark settings.
- `summary.json` and `summary.csv`: aggregate medians with repetition min/max values.
- Four PNG files: latency, throughput, CPU, and subscriber-scaling plots.

All 441 runs and all 147 aggregate groups were valid. The raw run tree was kept under
`.work/phase_8_1/full_matrix/phase8_1_full_971129a` during validation and is intentionally not
committed; it is removed by the Phase 8.1 cleanup step after these compact artifacts are saved.
The original `phase8_reference/` remains unchanged for historical comparison.
