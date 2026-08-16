# Automated Benchmark

## Goals And Boundary

Phase 8 measures the existing local cross-process data paths before any profiling or optimization.
It compares correctness, steady-state latency, delivered throughput, CPU, RSS, loss, overflow,
allocation failure, and blocking across four implementations. A result is useful whether custom
middleware is faster or slower than ROS2; correctness and a reproducible comparison are the
acceptance criteria.

Phase 8 does not measure startup/discovery latency, use `perf` or `strace`, tune the scheduler, pin
CPUs, add lock-free structures, or change transport algorithms in response to a score.

## Transport Definitions

| Name | Publisher path | Subscriber path |
| --- | --- | --- |
| `uds` | prebuilt bytes -> `Publisher::publish()` -> UDS | owning receive payload |
| `shm_copy` | prebuilt bytes -> `Publisher::publish()` -> pool copy | `SampleView` |
| `shm_loan` | direct fill of `LoanedSample::data()` -> publish | `SampleView` |
| `ros2` | direct `rclcpp` publish of `UInt8MultiArray` | direct `rclcpp` subscription |

The two SHM modes share the registry, memory pool, bounded handle queues, and subscriber view code.
Their intended difference is the publisher input path. SHM copy does not add an owning subscriber
copy. SHM loan does not allocate a full-size application vector and copy it into the loan.

The ROS2 baseline is an independent ament package under `benchmark/ros2`. It uses ROS2 Jazzy,
`rmw_fastrtps_cpp`, Reliable reliability, KeepLast history, and the configured depth. It does not
link the custom core, start `mw_registryd`, or use either Phase 7 bridge. A bridge would measure
adapter serialization plus two transports rather than direct ROS2 communication.

## Fairness Controls

Every compared main case uses the same host session, source revision, exact application payload
size, topology, phase durations, profile, and logical payload. The runner interleaves transports
with a deterministic shuffle seed recorded in `config.json`; it does not run every transport in a
single fixed block. ROS2 uses an isolated recorded `ROS_DOMAIN_ID` and localhost discovery.

All performance binaries use Release builds with sanitizers and coverage disabled. Debug, ASan,
UBSan, and Phase 7 adapter tests are correctness regressions only and their numbers are never used
as performance results. Normal/default Fast DDS behavior is retained; no special Fast DDS SHM
profile is enabled.

The main comparison uses queue depth 8. SHM uses `BLOCK_WITH_TIMEOUT` with a 100 ms timeout. ROS2
uses Reliable KeepLast depth 8. UDS has a socket connection per subscriber and does not have the
SHM ring policy. These settings seek delivered communication but ROS QoS and middleware overflow
policy are not claimed to be semantically identical.

The SHM pool is finite. Each existing default class count is raised only when needed to
`max(default_count, queue_depth + 2)`. At main depth 8 the 256 B, 4 KiB, 64 KiB, 1 MiB, and 4 MiB
classes therefore contain 32, 16, 10, 10, and 10 chunks (about 51 MiB total payload capacity).
This supports one full queue plus publication/view overlap without unbounded growth. The focused
depth-2 experiment uses 32, 16, 8, 4, and 4. Each SHM `summary.json` records the exact list.

## Payload And Clock Boundary

The configured size is the exact application byte count for every transport, including the bytes
inside the ROS `UInt8MultiArray`. Its explicitly encoded envelope is:

```text
offset 0..7    sequence, unsigned 64-bit big-endian
offset 8..15   CLOCK_MONOTONIC publish timestamp ns, unsigned 64-bit big-endian
offset 16..N   deterministic byte pattern derived from sequence and offset
```

The publisher fills the sequence and pattern first. Immediately before the transport call it
captures a monotonic timestamp, writes it into bytes 8..15, and calls UDS publish, SHM copy
publish, loan publish, or ROS publish. Application payload generation is therefore outside the
latency boundary. The subscriber captures the same monotonic clock immediately after the receive
API/callback exposes the application message, before validation or optional slow-consumer delay.

Each subscriber checks exact size, sequence, timestamp ordering, and every deterministic payload
byte. Gaps are recorded separately as loss. Duplicates and out-of-order values are sequence
errors. Only correctly validated messages contribute to delivered throughput.

## Profiles And Matrix

The exact size matrix is 64, 1024, 4096, 65536, 1048576, and 4194304 bytes. Each transport uses
one publisher process and 1, 2, or 4 independent subscriber processes.

The latency profile applies the same configured fixed offered rate to all transports for a given
size and topology. Full defaults are 1000 Hz through 4 KiB, 500 Hz at 64 KiB, 50 Hz at 1 MiB, and
10 Hz at 4 MiB. The throughput profile publishes as fast as the implementation safely permits for
the fixed window.

Every full main case has a 2 second warmup, 5 second measurement, 1 second cooldown, and three
repetitions. Startup, registry/pool construction, endpoint discovery, and ROS discovery complete
before warmup. Only sequences in the distinct measurement range enter summaries.

For latency, every correctly received measurement message is eligible for sampling. For
throughput, the first correct message and every 1000th thereafter are sampled. Samples are stored
in preallocated bounded memory and written after delivery stops. The cap is 1,000,000 samples; a
run is invalid rather than silently truncated if the cap is exceeded. This sampling affects only
the latency distribution, never message/byte throughput accounting.

## Metrics

Latency is `receive_monotonic_ns - publish_monotonic_ns`. Within a run, p50/p90/p99 sort integer
nanoseconds and linearly interpolate at `(sample_count - 1) * quantile`. Across three repetitions,
each scalar metric reports the median, minimum, and maximum; the best run is not selected and high
latency samples are not removed.

For one-to-N cases the runner reports all of the following, because a single throughput value is
ambiguous:

- publisher logical rate: successful logical publications once per message;
- per-subscriber correct messages/s and delivered MiB/s;
- aggregate delivery: the sum of correct deliveries and bytes across all subscribers.

`MB_per_second` fields use 1 MiB = 1,048,576 bytes. Per-run latency includes aggregate and each
subscriber p50/p90/p99 plus worst-subscriber p99.

The runner snapshots publisher and each subscriber's user+system CPU ticks from `/proc/<pid>/stat`
at the acknowledged measurement boundaries. Percent CPU is tick delta divided by wall duration
and `SC_CLK_TCK`; publisher and each subscriber remain separate. RSS is sampled from
`/proc/<pid>/status` every 100 ms by default and summarized as mean and peak. Registry CPU is not
included in publisher CPU.

Publication summaries also retain offered/published/received counts, drops, sequence gaps,
duplicates/out-of-order errors, queue overflows, pool allocation failures, exact blocked operation
count, and exact nanoseconds spent in the SHM blocking wait. Drop rate uses offered endpoint
deliveries (`publish_attempts * subscriber_count`) as its denominator; successful publisher
logical throughput continues to use `messages_published`.

## Backpressure Experiment

The secondary experiment does not multiply the main matrix. It uses SHM copy, 64 KiB, one slow
subscriber (2 ms delay), queue depth 2, maximum-rate throughput, and three repetitions for each of
`DROP_NEWEST`, `DROP_OLDEST`, and `BLOCK_WITH_TIMEOUT` (1 ms). It reports offered and delivered
messages, p50/p90/p99, drops, overflow, blocking, and throughput. Drop-heavy offered throughput is
not treated as equivalent to fully delivered throughput.

## Process And Resource Model

The Python runner owns exact child PIDs and starts a per-case registry only for custom transports.
It creates unique topics, UDS paths, ROS domains, and run directories; waits for subscriber-ready
markers; and requires the ROS publisher to observe the expected subscription count. File-marker
acknowledgements align CPU/RSS snapshots to the publisher's measurement window.

Every readiness, measurement, child exit, and shutdown wait is bounded. Cleanup signals only the
runner's exact child PIDs, checks only case-created socket paths, and compares the project SHM
namespace before/after the case. It never wildcard-deletes SHM or broadly kills ROS processes.
Phase 6 owner/registry cleanup remains responsible for normal resources.

A run is invalid if a subscriber is not ready, a process crashes or exits nonzero, measurement is
short, an artifact is missing, payload/sequence corruption occurs, latency storage overflows, or a
test resource remains. Sequence gaps and policy drops remain valid when explicitly accounted.

## Results

Raw data is local and ignored by Git:

```text
benchmark/results/<run_id>/
  machine.json
  config.json
  <transport>/<size>/1_to_<N>/<profile>/run_<RR>/
    case_config.json
    raw_latency.csv
    summary.json
    cpu.csv
    memory.csv
    publisher.json
    subscriber_<N>.json
    *.stdout.log
    *.stderr.log
  backpressure/<policy>/run_<RR>/
  aggregated/
    summary.json
    summary.csv
    latency_vs_message_size.png
    throughput_vs_message_size.png
    cpu_vs_message_size.png
    subscriber_count_vs_throughput.png
```

`machine.json` omits hostname, username, and home directory while recording OS/kernel/WSL,
architecture, CPU, logical CPU count, total memory, compiler, CMake, Python, build type/flags, Git
revision/dirty state, ROS distro/prefix, RMW, and the effective configuration. Per-run summaries
retain complete correctness and process exit status. Aggregate files contain one case per repeated
identity with median/min/max values and links to all three run paths.

## Build And Run

Build the custom Release benchmark:

```bash
cmake -S . -B .work/phase_8/build_release \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DENABLE_ASAN=OFF \
  -DENABLE_UBSAN=OFF
cmake --build .work/phase_8/build_release -j
```

Build the direct ROS2 benchmark:

```bash
source /opt/ros/jazzy/setup.bash
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
colcon --log-base .work/phase_8/ros2/log build \
  --base-paths benchmark/ros2 \
  --build-base .work/phase_8/ros2/build \
  --install-base .work/phase_8/ros2/install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
```

Run the smoke matrix:

```bash
source /opt/ros/jazzy/setup.bash
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
python3 benchmark/python/run_benchmarks.py \
  --config benchmark/configs/smoke.json
```

Run one exact main case (filters disable the secondary backpressure cases):

```bash
python3 benchmark/python/run_benchmarks.py \
  --config benchmark/configs/full.json \
  --transport shm_loan \
  --message-size 65536 \
  --subscribers 4 \
  --profile throughput \
  --repetitions 1
```

Run the complete 432-case main matrix plus 9 backpressure runs:

```bash
python3 benchmark/python/run_benchmarks.py \
  --config benchmark/configs/full.json
```

Revalidate and regenerate aggregate files and plots without touching raw data:

```bash
python3 benchmark/python/analyze_results.py benchmark/results/<run_id>
python3 benchmark/python/plot_results.py benchmark/results/<run_id>
```

Use `--dry-run` to inspect deterministic case expansion and `--no-plots` when only validating
orchestration. Filters also support one transport, message size, topology, or profile.

## Phase 8.1 Profiling

Phase 8.1 reuses the same Release endpoints, payloads, queue settings, timings, and runner. Its
representative custom matrix covers 64 B, 4 KiB, 64 KiB, 1 MiB, and 4 MiB 1-to-1 plus 64 KiB and
4 MiB 1-to-4 where applicable. Direct ROS2 context covers 64 KiB and 4 MiB 1-to-1. Focused SHM
before/after validation uses three repetitions for both latency and throughput.

On the recorded WSL2 host, neither `perf` nor `strace` was installed. The profiling report does not
claim symbol hotspots or syscall counts. The bounded fallback observer records measurement-window
CPU ticks, page faults, voluntary/nonvoluntary context switches, and 100 ms wait-channel samples
for the exact benchmark child processes. Reproduce it with:

```bash
source /opt/ros/jazzy/setup.bash
source .work/phase_8_1/ros2_profile/install/setup.bash
python3 benchmark/profiling/run_phase8_1_profile.py \
  --output-root .work/phase_8_1/profile \
  --build-dir .work/phase_8_1/build_profile \
  --ros-install .work/phase_8_1/ros2_profile/install
```

Compact profiling evidence is under `benchmark/profiling/`. The original Phase 8 aggregate stays
under `benchmark/results/phase8_reference/`; the optimized complete-matrix aggregate is separate
under `benchmark/results/phase8_1_reference/`. See
`docs/reports/PHASE_8_1_REPORT.md` for attribution, copy-path analysis, limitations, and acceptance.

## Interpretation And Limitations

Compare latency-profile p50/p99 only at the same configured rate, size, and topology. Compare
publisher logical throughput separately from aggregate delivered throughput, especially for
one-to-four. Use CPU and loss/error counters beside throughput, and use the reported min/max range
to judge repetition variability. SHM copy versus loan isolates publisher payload preparation;
SHM versus UDS additionally changes kernel socket payload transfer, queueing, and receive API.

Results describe one WSL/native host session and normal OS scheduling, not hard real-time bounds.
No CPU isolation, affinity, priority, cache conditioning, or background-load control is applied.
ROS2 `UInt8MultiArray` serialization and DDS protocol overhead are included outside the equal
application byte count. ROS QoS and custom backpressure are similar comparison settings, not
equivalent guarantees. Sampled throughput latency is not a complete tail distribution. The Phase 8
aggregate alone does not attribute causes to syscalls, copies, locks, scheduling, or cache behavior;
the separate Phase 8.1 evidence records the available attribution and its limits.
