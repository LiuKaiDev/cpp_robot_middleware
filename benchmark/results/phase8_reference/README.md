# Phase 8 Reference Results

These compact artifacts were generated from the full local run
`benchmark/results/phase8_full_cf35309` on Git commit
`cf353091f7149ac4f458ec77d98f23bb948155b2`.

- `machine.json`: sanitized host, toolchain, Git, ROS, RMW, and effective configuration metadata.
- `config.json`: complete deterministic case order for 432 main and 9 backpressure runs.
- `summary.json`: 147 repeated case groups with median/min/max metrics.
- `summary.csv`: flattened aggregate metrics for external inspection.
- Four PNG files: mandatory latency, throughput, CPU, and subscriber-scaling plots.

The 441 per-run directories and raw latency CSV files total about 106 MiB and remain local under
the ignored full-run directory. They are not committed. No raw measurement was edited or filtered;
`summary.json` contains relative run paths back to every repetition.
