# Phase 8 Report

## Scope

Phase 8 adds an automated, correctness-gated, local cross-process benchmark for custom UDS,
custom SHM copy, custom SHM loan, and direct ROS2 Jazzy with `rmw_fastrtps_cpp`. It measures fixed
offered-load latency, maximum-rate delivered throughput, process CPU, RSS, loss, allocation,
overflow, blocking, topology scaling, and a focused slow-subscriber backpressure experiment.

No transport was optimized in response to these results. No `perf`, `strace`, cache profiling,
lock-free structure, eventfd change, CPU affinity, scheduler tuning, or Fast DDS special transport
profile was used.

## Files Added

- `CODEX_TASKS/PHASE_8.md`
- `benchmark/configs/full.json`
- `benchmark/configs/smoke.json`
- `benchmark/cpp/CMakeLists.txt`
- `benchmark/cpp/benchmark_common.hpp`
- `benchmark/cpp/benchmark_common.cpp`
- `benchmark/cpp/mw_bench_publisher.cpp`
- `benchmark/cpp/mw_bench_subscriber.cpp`
- `benchmark/python/benchmark_analysis.py`
- `benchmark/python/run_benchmarks.py`
- `benchmark/python/analyze_results.py`
- `benchmark/python/plot_results.py`
- `benchmark/python/collect_machine_info.py`
- `benchmark/python/tests/test_analysis.py`
- `benchmark/ros2/mw_ros2_benchmark/` direct ROS2 package
- `benchmark/results/.gitkeep`
- `benchmark/results/phase8_reference/` sanitized aggregate artifacts and plots
- `tests/unit/benchmark_common_test.cpp`
- `tests/integration/uds_multi_subscriber_test.cpp`
- `docs/BENCHMARK.md`
- `PHASE_8_REPORT.md`

## Files Modified

- `.gitignore`: ignores raw benchmark, build, Python bytecode, and colcon log output while allowing
  compact reference artifacts.
- `CMakeLists.txt`, `tests/CMakeLists.txt`: build benchmark endpoints and add benchmark/UDS tests.
- `README.md`: updates status, benchmark commands, methodology, results, and limitations.
- `middleware/include/mw/result.hpp`: exposes exact blocked count/time in `PublishResult`.
- `middleware/src/detail/queue_protocol.hpp`, `subscriber_queue.hpp`, `subscriber_queue.cpp`:
  queue layout v3 and exact monotonic block instrumentation.
- `middleware/src/publisher.cpp`: minimum UDS discovery fanout and block metric propagation.
- `registry/src/registry_state.cpp`: validates queue layout v3.
- Registry/queue unit fixtures: use layout v3 and assert block metrics.
- `docs/CONTROL_PLANE.md`, `docs/FAILURE_MODEL.md`, `docs/QUEUES_AND_LOANING.md`: document UDS
  fanout and queue layout v3.
- `benchmark/python/benchmark_analysis.py` and its test: post-run aggregate also retains offered
  `publish_attempts`; raw measurements were not changed.

## Measured Revision And Build

- Benchmark Git commit: `cf353091f7149ac4f458ec77d98f23bb948155b2`
- Commit subject: `feat: add automated phase 8 benchmark harness`
- Working tree at capture: clean (`git_dirty=false` in `machine.json`)
- Root build: `CMAKE_BUILD_TYPE=Release`, `BUILD_TESTING=OFF`
- Release flags: `-O3 -DNDEBUG`
- ASan: OFF
- UBSan: OFF
- Coverage/TSan: not enabled
- Direct ROS2 package: colcon Release build

The post-measurement addition of `publish_attempts` to aggregate JSON is a deterministic reduction
of the already recorded per-run field. It did not alter or rerun any raw measurement. All four
transports were measured from the same committed binaries and source revision above.

## Machine And ROS Environment

| Item | Recorded value |
| --- | --- |
| Host type | WSL2 Linux |
| Kernel | `6.18.33.2-microsoft-standard-WSL2` |
| Architecture | x86_64 |
| CPU | Intel Core i5-8300H @ 2.30 GHz |
| Logical CPUs | 8 |
| Memory | 8,260,288,512 bytes |
| Compiler | GCC 13.3.0 |
| CMake | 3.28.3 |
| Python | 3.12.3 |
| matplotlib | 3.6.3 |
| ROS_DISTRO | `jazzy` |
| rclcpp | 28.1.21 |
| std_msgs | 5.3.8 |
| RMW | `rmw_fastrtps_cpp` 8.4.4 |
| ROS prefix | `/opt/ros/jazzy` |
| ROS_DOMAIN_ID | 181 |
| Discovery range | `LOCALHOST` |

No hostname, username, or home directory is present in the committed reference metadata.

## Transport Definitions

- UDS: prebuilt application bytes -> `Publisher::publish()` -> UDS frame -> owning subscriber
  receive. No SHM or loan API is used. One encoded logical frame is sent to each discovered UDS
  endpoint.
- SHM Copy: prebuilt application bytes -> `Publisher::publish()` -> one pool payload copy ->
  subscriber `SampleView`. No intentional subscriber vector copy is added.
- SHM Loan: application writes directly into `LoanedSample::data()` -> loan publish -> subscriber
  `SampleView`. No second full payload source buffer is copied into the loan.
- ROS2: direct cross-process `rclcpp` publisher/subscribers using
  `std_msgs/msg/UInt8MultiArray`, Reliable, KeepLast, depth 8, and
  `rmw_fastrtps_cpp`. It does not use the Phase 7 adapter, custom registry, pool, or core.

## Payload, Matrix, And Configuration

Every transport carries exactly the configured application bytes:

```text
0..7    uint64 sequence, big-endian
8..15   uint64 CLOCK_MONOTONIC publish timestamp ns, big-endian
16..N   deterministic byte pattern from sequence and offset
```

All bytes except the timestamp are prepared first. The timestamp is written immediately before
the transport API call. The subscriber captures monotonic receive time before validation. It then
checks exact size, timestamp ordering, sequence, and every payload byte.

- Sizes: 64, 1024, 4096, 65536, 1048576, and 4194304 bytes.
- Topologies: one publisher process to 1, 2, or 4 independent subscriber processes.
- Profiles: fixed-rate latency and maximum-rate throughput.
- Timing: 2 s warmup, 5 s measurement, 1 s cooldown.
- Repetitions: 3 per main case.
- Main cases: 4 x 6 x 3 x 2 x 3 = 432.
- Secondary cases: 3 policies x 3 repetitions = 9.
- Main queue: depth 8, SHM `BLOCK_WITH_TIMEOUT`, timeout 100 ms.
- Latency rates: 1000 Hz through 4 KiB, 500 Hz at 64 KiB, 50 Hz at 1 MiB, and
  10 Hz at 4 MiB.
- Latency samples: every correct receive for latency; first then every 1000th correct receive for
  throughput; hard cap 1,000,000 with run invalidation on overflow.
- Main SHM pool class counts: 32, 16, 10, 10, 10 for 256 B, 4 KiB, 64 KiB, 1 MiB, 4 MiB.

ROS Reliable KeepLast and SHM BLOCK_WITH_TIMEOUT are comparison settings, not claimed equivalent
QoS semantics.

## Measurement And Statistics

Latency is `receive_monotonic_ns - publish_monotonic_ns`. Raw samples stay in bounded memory and
are written after the hot path. Within-run percentiles sort integer nanoseconds and linearly
interpolate at `(n - 1) * q`. Each aggregate metric is the median of the three repetitions and
also retains its minimum and maximum. No sample or repetition was deleted.

Publisher logical throughput counts successful logical publications once. Per-subscriber
throughput counts only correct receives. Aggregate delivered throughput sums correct bytes over
all subscribers. `MB_per_second` fields are MiB/s (1,048,576 bytes).

CPU is user+system tick delta from `/proc/<pid>/stat` at acknowledged measurement boundaries,
divided by `SC_CLK_TCK` and elapsed monotonic time. RSS is sampled from `/proc/<pid>/status` every
100 ms and stored as mean and peak. CPU totals may exceed 100% because publisher and independent
subscriber processes can occupy multiple cores.

## Correctness And Acceptance

| Acceptance | Result |
| --- | --- |
| Core Debug regression | PASS, 81/81 |
| Core ASan, leak detection enabled | PASS, 81/81 |
| Core UBSan, halt on diagnostic | PASS, 81/81 |
| Installed package/external consumer | PASS, printed `0.1.0` |
| Phase 7 adapter colcon regression | PASS, 24 results, 0 failures |
| Direct ROS2 benchmark Release build | PASS |
| Committed-revision smoke | PASS, 16/16 |
| Full main matrix | PASS, 432/432 |
| Backpressure matrix | PASS, 9/9 |
| Aggregate validation | PASS, 441/441 and 147/147 groups |
| Required PNG plots | PASS, four 1440x880 images |

Full run wall time was approximately 62 minutes 14 seconds. Across all 432 main repetitions:

- payload errors: 0;
- duplicate/out-of-order sequence errors: 0;
- allocation failures: 0;
- custom UDS/SHM inferred drops: 0;
- SHM queue timeouts/overflows: 0;
- bounded latency sample omissions: 0;
- minimum measurement elapsed time: 5.000001179 s.

Sequence gaps are valid loss accounting, not sequence corruption. Maximum-rate ROS2 produced
recorded gaps; these are reported below and only correct deliveries enter throughput.

## 1-to-1 Fixed-Rate Latency

Values are microseconds, median over three repetitions. The p99 range is repetition min-max.

| Size | Transport | p50 | p90 | p99 | p99 min-max |
| ---: | --- | ---: | ---: | ---: | ---: |
| 64 | UDS | 81.2 | 113.6 | 184.9 | 156.3-196.0 |
| 64 | SHM Copy | 212.2 | 287.1 | 376.0 | 363.2-437.3 |
| 64 | SHM Loan | 207.3 | 266.4 | 369.0 | 337.8-418.4 |
| 64 | ROS2 | 153.8 | 216.7 | 330.6 | 290.1-341.3 |
| 1024 | UDS | 78.0 | 111.9 | 162.6 | 153.4-197.9 |
| 1024 | SHM Copy | 215.8 | 301.5 | 405.4 | 369.1-466.5 |
| 1024 | SHM Loan | 210.0 | 286.7 | 389.2 | 350.0-444.6 |
| 1024 | ROS2 | 152.6 | 215.5 | 326.8 | 283.4-375.2 |
| 4096 | UDS | 81.3 | 118.4 | 190.4 | 152.0-224.9 |
| 4096 | SHM Copy | 216.7 | 290.7 | 405.8 | 369.1-423.2 |
| 4096 | SHM Loan | 203.9 | 254.3 | 339.7 | 333.1-415.1 |
| 4096 | ROS2 | 154.0 | 212.4 | 295.2 | 282.6-366.0 |
| 65536 | UDS | 103.7 | 134.8 | 218.7 | 190.9-271.2 |
| 65536 | SHM Copy | 245.6 | 322.6 | 497.5 | 463.3-522.1 |
| 65536 | SHM Loan | 247.1 | 308.7 | 435.8 | 416.0-461.8 |
| 65536 | ROS2 | 181.5 | 253.6 | 326.0 | 318.9-478.8 |
| 1048576 | UDS | 747.0 | 914.5 | 1049.6 | 921.8-1072.5 |
| 1048576 | SHM Copy | 403.3 | 533.5 | 717.1 | 679.9-802.5 |
| 1048576 | SHM Loan | 278.9 | 334.0 | 476.3 | 441.8-492.1 |
| 1048576 | ROS2 | 11206.0 | 11312.0 | 11483.5 | 11462.9-12165.9 |
| 4194304 | UDS | 1781.4 | 2775.8 | 3096.2 | 2784.6-3200.1 |
| 4194304 | SHM Copy | 623.0 | 763.4 | 970.1 | 918.9-1104.0 |
| 4194304 | SHM Loan | 272.5 | 312.5 | 352.8 | 329.9-437.9 |
| 4194304 | ROS2 | 11895.9 | 12237.0 | 13172.9 | 12624.3-18701.0 |

## 1-to-1 Maximum-Rate Throughput

All values are medians. Messages/s is correct delivery per subscriber. Logical and delivered
columns are MiB/s. Drop percent uses offered endpoint deliveries.

| Size | Transport | msg/s | Logical | Delivered | Drop % |
| ---: | --- | ---: | ---: | ---: | ---: |
| 64 | UDS | 150400 | 9.2 | 9.2 | 0.00 |
| 64 | SHM Copy | 10297 | 0.6 | 0.6 | 0.00 |
| 64 | SHM Loan | 10060 | 0.6 | 0.6 | 0.00 |
| 64 | ROS2 | 22105 | 2.0 | 1.3 | 30.64 |
| 1024 | UDS | 131215 | 128.1 | 128.1 | 0.00 |
| 1024 | SHM Copy | 10257 | 10.0 | 10.0 | 0.00 |
| 1024 | SHM Loan | 10202 | 10.0 | 10.0 | 0.00 |
| 1024 | ROS2 | 22029 | 30.3 | 21.5 | 27.06 |
| 4096 | UDS | 96599 | 377.3 | 377.3 | 0.00 |
| 4096 | SHM Copy | 10235 | 40.0 | 40.0 | 0.00 |
| 4096 | SHM Loan | 10186 | 39.8 | 39.8 | 0.00 |
| 4096 | ROS2 | 21467 | 105.4 | 83.9 | 20.40 |
| 65536 | UDS | 17657 | 1103.5 | 1103.5 | 0.00 |
| 65536 | SHM Copy | 7920 | 495.0 | 495.0 | 0.00 |
| 65536 | SHM Loan | 8197 | 512.3 | 512.3 | 0.00 |
| 65536 | ROS2 | 8263 | 536.8 | 516.4 | 3.80 |
| 1048576 | UDS | 1182 | 1181.9 | 1181.9 | 0.00 |
| 1048576 | SHM Copy | 1435 | 1435.1 | 1435.1 | 0.00 |
| 1048576 | SHM Loan | 1522 | 1522.4 | 1522.4 | 0.00 |
| 1048576 | ROS2 | 1010 | 1379.8 | 1010.2 | 26.79 |
| 4194304 | UDS | 293 | 1170.3 | 1170.3 | 0.00 |
| 4194304 | SHM Copy | 387 | 1546.3 | 1546.3 | 0.00 |
| 4194304 | SHM Loan | 398 | 1593.1 | 1593.1 | 0.00 |
| 4194304 | ROS2 | 251 | 1202.6 | 1005.1 | 16.21 |

## CPU Table

Selected 1-to-1 throughput sizes show the small endpoint, the specified 64 KiB scaling point, and
the largest endpoint. Values are process CPU percent medians.

| Size | Transport | Publisher | Subscriber | Combined |
| ---: | --- | ---: | ---: | ---: |
| 64 | UDS | 40.5 | 100.4 | 140.9 |
| 64 | SHM Copy | 58.9 | 20.2 | 79.0 |
| 64 | SHM Loan | 60.1 | 20.0 | 80.0 |
| 64 | ROS2 | 114.5 | 120.5 | 235.1 |
| 65536 | UDS | 50.5 | 99.0 | 149.5 |
| 65536 | SHM Copy | 64.9 | 48.1 | 113.0 |
| 65536 | SHM Loan | 63.9 | 49.1 | 113.0 |
| 65536 | ROS2 | 114.6 | 118.3 | 232.9 |
| 4194304 | UDS | 55.5 | 95.8 | 151.3 |
| 4194304 | SHM Copy | 60.3 | 99.8 | 160.1 |
| 4194304 | SHM Loan | 46.7 | 99.4 | 146.1 |
| 4194304 | ROS2 | 111.0 | 122.3 | 233.3 |

At 4 MiB, SHM Loan delivered 3.0% more than SHM Copy while using 8.7% less combined process CPU
(146.1% versus 160.1%). This is a measured relationship, not a bottleneck attribution.

## RSS Table

Values are median peak RSS in MiB for 1-to-1 throughput. SHM RSS includes mapped finite pool pages;
it is not heap-only memory.

| Size | Transport | Publisher | Subscriber | Total |
| ---: | --- | ---: | ---: | ---: |
| 64 | UDS | 4.1 | 4.1 | 8.3 |
| 64 | SHM Copy | 4.4 | 4.4 | 8.9 |
| 64 | SHM Loan | 4.5 | 4.4 | 8.9 |
| 64 | ROS2 | 27.2 | 27.3 | 54.5 |
| 65536 | UDS | 4.2 | 4.2 | 8.4 |
| 65536 | SHM Copy | 5.1 | 5.0 | 10.1 |
| 65536 | SHM Loan | 5.1 | 5.0 | 10.1 |
| 65536 | ROS2 | 27.8 | 28.3 | 56.1 |
| 4194304 | UDS | 8.2 | 8.1 | 16.3 |
| 4194304 | SHM Copy | 48.4 | 44.4 | 92.8 |
| 4194304 | SHM Loan | 48.4 | 44.4 | 92.8 |
| 4194304 | ROS2 | 67.4 | 67.7 | 135.0 |

## Drop, Error, Overflow, And Block Table

Totals below sum all 108 main repetitions per transport. Drop count is endpoint deliveries, so it
must be read with the per-case drop rate in aggregate results.

| Transport | Payload errors | Sequence errors | Drops | Queue overflow | Allocation failure | Blocked count | Blocked time s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| UDS | 0 | 0 | 0 | 0 | 0 | 0 | 0.00 |
| SHM Copy | 0 | 0 | 0 | 0 | 0 | 66,345 | 34.28 |
| SHM Loan | 0 | 0 | 0 | 0 | 0 | 72,345 | 43.95 |
| ROS2 | 0 | 0 | 3,143,087 | 0 | 0 | 0 | 0.00 |

SHM main-profile waits all completed before the 100 ms timeout, hence blocked time without queue
overflow or loss. ROS2 loss occurred predominantly in maximum-rate profiles: 1-to-1 median drop
rates ranged from 3.80% to 30.64% across sizes; 1-to-4 ranged from 0.32% to 61.53%. Fixed-rate ROS2
had zero median loss except sub-0.04% at three small 1-to-4 cases.

## Subscriber Scaling At 64 KiB

This is the representative size fixed in the mandatory scaling plot. Logical/aggregate values are
MiB/s medians; messages/s is per subscriber.

| Transport | N | Logical | Aggregate delivered | Per-sub msg/s | Drop % |
| --- | ---: | ---: | ---: | ---: | ---: |
| UDS | 1 | 1103.5 | 1103.5 | 17657 | 0.00 |
| UDS | 2 | 1036.0 | 2072.0 | 16576 | 0.00 |
| UDS | 4 | 513.0 | 2051.8 | 8207 | 0.00 |
| SHM Copy | 1 | 495.0 | 495.0 | 7920 | 0.00 |
| SHM Copy | 2 | 432.1 | 864.3 | 6914 | 0.00 |
| SHM Copy | 4 | 319.1 | 1276.4 | 5106 | 0.00 |
| SHM Loan | 1 | 512.3 | 512.3 | 8197 | 0.00 |
| SHM Loan | 2 | 426.5 | 852.9 | 6824 | 0.00 |
| SHM Loan | 4 | 304.0 | 1216.2 | 4865 | 0.00 |
| ROS2 | 1 | 536.8 | 516.4 | 8263 | 3.80 |
| ROS2 | 2 | 378.6 | 740.9 | 5927 | 2.15 |
| ROS2 | 4 | 307.6 | 1116.6 | 4466 | 9.83 |

From 1-to-1 to 1-to-4, publisher logical throughput changed by -53.5% UDS, -35.5% SHM Copy,
-40.7% SHM Loan, and -42.7% ROS2. Aggregate delivered throughput changed by 1.86x, 2.58x, 2.37x,
and 2.16x respectively. Aggregate scaling is not the same as publisher capacity.

Fixed-rate latency at the same point was:

| Transport | N | p50 us | p99 us |
| --- | ---: | ---: | ---: |
| UDS | 1 | 103.7 | 218.7 |
| UDS | 2 | 118.4 | 246.2 |
| UDS | 4 | 148.3 | 361.4 |
| SHM Copy | 1 | 245.6 | 497.5 |
| SHM Copy | 2 | 277.5 | 516.1 |
| SHM Copy | 4 | 299.3 | 567.8 |
| SHM Loan | 1 | 247.1 | 435.8 |
| SHM Loan | 2 | 254.2 | 468.1 |
| SHM Loan | 4 | 283.0 | 495.1 |
| ROS2 | 1 | 181.5 | 326.0 |
| ROS2 | 2 | 219.0 | 450.8 |
| ROS2 | 4 | 308.6 | 989.5 |

Thus p99 increased 1.65x UDS, 1.14x SHM Copy, 1.14x SHM Loan, and 3.04x ROS2 at this one
representative size. Complete 1-to-2 and 1-to-4 results for all sizes are in reference JSON/CSV.

## Backpressure Experiment

The experiment used SHM Copy, 64 KiB, one subscriber delayed 2 ms per message, queue depth 2,
maximum-rate publication, and 1 ms block timeout. Every value is a separate median over three
runs; medians from different rows/fields need not arithmetically reconcile.

| Policy | Offered | Published | Delivered | Drop % | Overflow | Blocked | Blocked ms | p50 us | p90 us | p99 us | MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| DROP_NEWEST | 52021 | 2313 | 2313 | 95.53 | 49697 | 0 | 0.0 | 4368.0 | 4411.4 | 4414.5 | 28.9 |
| DROP_OLDEST | 51527 | 51527 | 2307 | 95.52 | 49221 | 0 | 0.0 | 215.0 | 258.3 | 268.1 | 28.8 |
| BLOCK_WITH_TIMEOUT | 4486 | 2243 | 2243 | 49.99 | 2243 | 4479 | 4131.5 | 5249.1 | 5345.2 | 5385.6 | 28.0 |

All policies delivered about 28-29 MiB/s because the intentionally delayed subscriber was the
delivery limit. DROP_OLDEST retained the most recent queued values and measured 0.215 ms p50 while
dropping 95.52% of offered endpoint deliveries. DROP_NEWEST preserved queued values and measured
4.368 ms p50 with 95.53% drop. BLOCK reduced offered attempts by waiting 4.132 s of the 5 s window,
measured 5.249 ms p50, and still timed out about half of offered attempts. Drop-heavy offered work
is not presented as delivered throughput.

## UDS Versus SHM

The data does not support a blanket statement that SHM is faster.

- At 64 B through 64 KiB, UDS had 2.37x-2.77x lower 1-to-1 p50 latency than SHM Copy and
  2.15x-14.61x its delivered throughput.
- At 1 MiB, SHM Copy p50 was 1.85x lower and throughput 21.4% higher than UDS. SHM Loan p50 was
  2.68x lower and throughput 28.8% higher.
- At 4 MiB, SHM Copy p50 was 2.86x lower and throughput 32.1% higher. SHM Loan p50 was 6.54x lower
  and throughput 36.1% higher.

These are end-to-end measurements of the current implementation. Phase 8 does not attribute the
small-message result to any specific syscall, lock, wake, scheduler, or queue cost.

## SHM Copy Versus SHM Loan

Loan changed little at small sizes. Relative to Copy, its p50 was 2.4%, 2.7%, and 6.3% lower at
64 B, 1 KiB, and 4 KiB, and 0.6% higher at 64 KiB. Their small-message delivered throughput differed
by at most 3.5%.

The large-message separation was clear: Loan p50 was 1.45x lower at 1 MiB and 2.29x lower at
4 MiB. Delivered throughput was 6.1% and 3.0% higher respectively. The benchmark isolates the
publisher input path while keeping the pool, queue, and subscriber SampleView path common.

## ROS2 Baseline

At 64 B through 64 KiB, ROS2 fixed-rate p50 was lower than both SHM variants but higher than UDS.
At 1 MiB and 4 MiB its p50 was 11.206 ms and 11.896 ms, compared with all custom paths below
1.782 ms. For delivered 1-to-1 throughput, ROS2 exceeded SHM at 64 B through 64 KiB, approximately
matched SHM at 64 KiB, and was below every custom path at 1 MiB and 4 MiB.

Maximum-rate ROS2 also showed application-level loss despite Reliable KeepLast: the table reports
only correct delivered messages and explicit drop rate. This does not imply ROS reliability is
incorrect; the benchmark uses finite history and a publisher without application acknowledgements.

## Variability And Outliers

Every aggregate contains all three repetitions with median/min/max. Across the 72 fixed-rate
latency groups, the median repetition span was 5.36% for p50 and 19.51% for p99; the 90th-percentile
group span was 11.62% and 38.33%, and maximum was 21.23% and 96.79%.

Across the 72 throughput groups, delivered-throughput span had 5.14% median, 13.07% 90th
percentile, and 18.27% maximum. Systematically sampled throughput latency was much noisier,
especially at large messages with few samples: p50 span median/max was 7.97%/596.47%, and p99 was
31.08%/528.30%. Therefore latency conclusions above use the fixed-rate profile, not sampled
maximum-rate tails.

No high latency sample was removed and no best repetition was selected. The committed aggregate
retains every case's min/max and relative paths to all repetitions.

## Artifacts

Local complete raw results (ignored by Git, about 106 MiB):

```text
benchmark/results/phase8_full_cf35309/
```

Committed sanitized reference artifacts:

```text
benchmark/results/phase8_reference/machine.json
benchmark/results/phase8_reference/config.json
benchmark/results/phase8_reference/summary.json
benchmark/results/phase8_reference/summary.csv
benchmark/results/phase8_reference/latency_vs_message_size.png
benchmark/results/phase8_reference/throughput_vs_message_size.png
benchmark/results/phase8_reference/cpu_vs_message_size.png
benchmark/results/phase8_reference/subscriber_count_vs_throughput.png
```

Each of the 441 local run directories contains `raw_latency.csv`, `summary.json`, `cpu.csv`, and
`memory.csv` plus endpoint artifacts/logs. Plot data comes only from aggregate JSON.

## Known Limitations

- Results represent one WSL2 host session under normal OS scheduling, not hard real-time bounds.
- CPU affinity, isolation, priority, frequency control, and background-load suppression were not
  applied.
- One-way monotonic timestamps are comparable on one kernel/host only.
- Throughput latency is systematically sampled and is not a full tail distribution.
- UDS writes subscriber sockets serially. SHM uses existing mutex/condition queues and UDS wakes.
- SHM RSS includes mapped preallocated pool pages and is configuration-dependent.
- ROS2 uses `UInt8MultiArray` and normal Fast DDS defaults; other types/RMWs/QoS may differ.
- ROS2 Reliable KeepLast and SHM overflow policy are not semantically identical.
- The benchmark records symptoms but does not identify syscall, copy, lock, cache, or scheduler
  causes.

## Phase Boundary

Phase 8.1 profiling was not implemented.

Phase 9 was not implemented.
