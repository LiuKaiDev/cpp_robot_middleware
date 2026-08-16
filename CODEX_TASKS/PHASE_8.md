# Codex Task - Phase 8: Automated Benchmark

## Scope

Implement Phase 8 only: build a reproducible local cross-process benchmark for custom UDS,
custom SHM copy, custom SHM loan, and direct ROS2 Jazzy with `rmw_fastrtps_cpp`. Preserve all
Phase 0-7 behavior. Do not profile or optimize the measured implementations.

## Required Matrix

- Application payload sizes: 64, 1024, 4096, 65536, 1048576, and 4194304 bytes.
- Topologies: one publisher to 1, 2, and 4 independent subscriber processes.
- Profiles: conservative fixed-rate latency and maximum safe throughput.
- Repetitions: at least three for every main case.
- Timing defaults: 2 seconds warmup, 5 seconds measurement, and 1 second cooldown.
- Secondary experiment: SHM copy with a slow subscriber and `DROP_NEWEST`, `DROP_OLDEST`, and
  `BLOCK_WITH_TIMEOUT`.

## Transport Boundaries

- UDS uses `Publisher::publish()` and the owning subscriber receive API without SHM.
- SHM copy uses a prebuilt application buffer, `Publisher::publish()`, and subscriber
  `SampleView` without an intentional subscriber payload copy.
- SHM loan writes the payload directly into `LoanedSample::data()`, publishes the loan, and uses
  subscriber `SampleView`.
- ROS2 uses direct cross-process `std_msgs/msg/UInt8MultiArray` publishers/subscribers. It must
  not use the Phase 7 adapter, custom registry, pool, or middleware API.

## Measurement Rules

- Use one exact-size byte envelope: big-endian sequence, big-endian monotonic publish timestamp,
  then deterministic sequence-and-offset payload bytes.
- Prepare/fill the payload before the timestamp and transport API call. Capture receive time as
  early as practical after application delivery.
- Keep warmup and cooldown outside all reported metrics. Keep file output outside the message hot
  path and use bounded, explicitly reported latency sampling.
- Derive process CPU from `/proc/<pid>/stat` tick deltas over the measurement window. Periodically
  sample RSS from `/proc/<pid>/status`.
- Mark runs invalid on corruption, duplicate/out-of-order sequence, incomplete duration,
  readiness or process failure, missing artifacts, or bounded sample overflow.
- Aggregate repetitions with median, minimum, and maximum. Never delete outliers or select the
  best repetition.

## Automation And Artifacts

- Support a smoke suite, CLI filters for one case/dimension, and the complete matrix.
- Use deterministic interleaved transport ordering and record its seed.
- Bound every readiness, measurement, and shutdown wait. Own exact PIDs and exact test resource
  names; never use broad process or namespace cleanup.
- Store raw local results by run ID. Every run writes `raw_latency.csv`, `summary.json`,
  `cpu.csv`, and `memory.csv`.
- Emit aggregate JSON/CSV and the four mandatory PNGs: latency, throughput, CPU, and subscriber
  scaling.
- Record sanitized machine, toolchain, Git, ROS, RMW, QoS, queue, matrix, and timing metadata.

## Verification And Delivery

- Add benchmark payload/statistics/schema tests and a UDS 1-to-4 regression test.
- Retain the complete core Debug, ASan, and UBSan suites and the Phase 7 colcon regression.
- Build measured binaries in Release without sanitizers, coverage, or Debug instrumentation.
- Run four-transport smoke, all 432 repeated main cases, and all 9 backpressure cases.
- Commit compact sanitized reference metadata, aggregate JSON/CSV, four plots, docs, and
  `PHASE_8_REPORT.md`; keep per-run raw data local and ignored.

## Deferred Work

Do not run `perf`, `strace`, cache profiling, or implement lock-free queues, allocator changes,
eventfd, affinity, scheduler tuning, or any other score-driven optimization. Phase 8.1 profiling
and Phase 9 final presentation are separate tasks.
