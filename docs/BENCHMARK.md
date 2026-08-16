# 自动 Benchmark

## 目标与边界

自动 Benchmark 对四种实现的最终本机跨进程 Data Path 进行比较，检查正确性、steady-state
latency、delivered throughput、CPU、RSS、loss、overflow、allocation failure 和 blocking。无论
自定义 Middleware 比 ROS2 更快还是更慢，结果都具有价值；正确性和可复现对比才是验收标准。

Startup/Discovery latency 不在矩阵内。Benchmark 不根据分数调整 Scheduler、绑定 CPU 或引入
lock-free structure。Profiling 运行及可用工具在后文单独说明。

## Transport 定义

| Name | Publisher path | Subscriber path |
| --- | --- | --- |
| `uds` | prebuilt bytes -> `Publisher::publish()` -> UDS | owning receive payload |
| `shm_copy` | prebuilt bytes -> `Publisher::publish()` -> pool copy | `SampleView` |
| `shm_loan` | direct fill of `LoanedSample::data()` -> publish | `SampleView` |
| `ros2` | direct `rclcpp` publish of `UInt8MultiArray` | direct `rclcpp` subscription |

两种 SHM mode 共享 Registry、Memory Pool、有界 Handle Queue 和 Subscriber View 代码；预期差异
仅在 Publisher input path。SHM Copy 不会额外执行 owning Subscriber copy；SHM Loan 不会分配
与消息等大的完整 application vector，再将其复制进 Loan。

ROS2 Baseline 是 `benchmark/ros2` 下的独立 ament package。它使用 ROS2 Jazzy、
`rmw_fastrtps_cpp`、Reliable reliability、KeepLast history 和已配置 Depth；不链接自定义 Core、
不启动 `mw_registryd`，也不使用 Adapter Bridge。经过 Bridge 会测量 Adapter serialization 加
两种 Transport，而不是 direct ROS2 communication。

## 公平性控制

所有被比较的主 Case 使用同一次 Host session、Source revision、精确 application payload size、
timing segment、profile 和逻辑 payload。Runner 按 `config.json` 中记录的确定性 shuffle seed
交错执行 Transport，不会把同一种 Transport 全部集中执行。ROS2 使用隔离并记录的
`ROS_DOMAIN_ID` 和 localhost Discovery。

所有 Performance binary 都是关闭 Sanitizer 和 Coverage 的 Release build。Debug、ASan、UBSan
和 Adapter 测试仅用于正确性回归，其数字绝不作为性能结果。保留 Fast DDS 的普通/默认行为，
不启用特殊 Fast DDS SHM profile。

主对比使用 Queue depth 8。SHM 使用 `BLOCK_WITH_TIMEOUT`，Timeout 为 100 ms；ROS2 使用
Reliable KeepLast depth 8。UDS 为每个 Subscriber 使用一条 Socket connection，没有 SHM Ring
Policy。这些设置力求完成消息传递，但不声称 ROS QoS 与 Middleware Overflow Policy 在语义上
等价。

SHM Pool 有限。仅在必要时，才把各现有默认 Class count 提高到
`max(default_count, queue_depth + 2)`。主矩阵 Depth 8 下，256 B、4 KiB、64 KiB、1 MiB 和
4 MiB Class 分别包含 32、16、10、10 和 10 个 Chunk（Payload 总容量约 51 MiB）。这足以支持
一个 Full Queue 加 Publish/View overlap，且不会无界增长。聚焦的 depth-2 实验分别使用
32、16、8、4 和 4。每个 SHM `summary.json` 都记录精确列表。

## Payload 与 Clock 边界

对于所有 Transport，配置的 Size 都是准确的 application byte count，包括 ROS
`UInt8MultiArray` 内的 byte。显式编码的 Envelope 为：

```text
offset 0..7    sequence, unsigned 64-bit big-endian
offset 8..15   CLOCK_MONOTONIC publish timestamp ns, unsigned 64-bit big-endian
offset 16..N   deterministic byte pattern derived from sequence and offset
```

Publisher 先填充 Sequence 和 Pattern。在调用 Transport 前的最后一刻，它获取 monotonic
timestamp，写入 byte 8..15，然后调用 UDS Publish、SHM Copy Publish、Loan Publish 或 ROS
Publish。因此 application payload generation 位于 Latency boundary 之外。Subscriber 在
Receive API/Callback 暴露 application message 后立即读取同一 monotonic clock，之后才执行
Validation 或可选的 Slow-consumer delay。

每个 Subscriber 校验精确 Size、Sequence、Timestamp ordering 和每个确定性 Payload byte。Gap
单独记录为 Loss；Duplicate 和 Out-of-order value 记录为 Sequence error。只有正确校验的消息
才计入 delivered throughput。

## Profile 与矩阵

精确 Size matrix 为 64、1024、4096、65536、1048576 和 4194304 bytes。每种 Transport 使用
一个 Publisher 进程和 1、2 或 4 个独立 Subscriber 进程。

对于给定 Size 和 Topology，Latency profile 向所有 Transport 应用相同的 configured fixed
offered rate。完整默认值是：4 KiB 及以下 1000 Hz，64 KiB 为 500 Hz，1 MiB 为 50 Hz，
4 MiB 为 10 Hz。Throughput profile 在固定窗口内以实现能够安全承受的最高速度 Publish。

每个完整主 Case 包含 2 second warmup、5 second measurement、1 second cooldown 和三次
repetition。Startup、Registry/Pool construction、Endpoint Discovery 和 ROS Discovery 都在
Warmup 前完成。Summary 只统计独立 measurement range 中的 Sequence。

对于 Latency，每条正确接收的 measurement message 都可进入 Sample；对于 Throughput，采样第一
条正确消息以及此后每第 1000 条消息。Sample 保存在预分配的有界 Memory 中，并在停止 Delivery
后写出。上限为 1,000,000 个 Sample；超过上限会使运行无效，而不是静默截断。Sampling 只影响
Latency distribution，不影响 Message/Byte throughput accounting。

## 指标

Latency 为 `receive_monotonic_ns - publish_monotonic_ns`。单次运行内，p50/p90/p99 对 integer
nanosecond 排序，并在 `(sample_count - 1) * quantile` 处执行 linear interpolation。三次
repetition 之间，每个 scalar metric 报告 median、minimum 和 maximum；不挑选最佳运行，也不
删除 high-latency sample。

对于 one-to-N case，Runner 会报告以下全部指标，因为单个 Throughput value 存在歧义：

- Publisher logical rate：每条消息只计算一次的 successful logical publication；
- per-Subscriber correct messages/s 和 delivered MiB/s；
- aggregate delivery：所有 Subscriber correct delivery 和 byte 之和。

`MB_per_second` field 使用 1 MiB = 1,048,576 bytes。per-run Latency 包含 aggregate 和每个
Subscriber 的 p50/p90/p99，以及 worst-Subscriber p99。

Runner 在确认过的 measurement boundary 从 `/proc/<pid>/stat` 读取 Publisher 和每个 Subscriber
的 user+system CPU tick。CPU 百分比为 Tick delta 除以 Wall duration 和 `SC_CLK_TCK`；
Publisher 与每个 Subscriber 分开统计。RSS 默认每 100 ms 从 `/proc/<pid>/status` 采样，并
汇总为 mean 和 peak。Publisher CPU 不包含 Registry CPU。

Publication Summary 还保留 offered/published/received count、Drop、Sequence gap、
Duplicate/Out-of-order error、Queue overflow、Pool allocation failure、精确 Blocked operation
count，以及在 SHM blocking wait 中花费的精确 nanosecond。Drop rate 使用 offered Endpoint
delivery（`publish_attempts * subscriber_count`）作为分母；Successful Publisher logical
throughput 继续使用 `messages_published`。

## Backpressure 实验

次要实验不会扩张主矩阵。它使用 SHM Copy、64 KiB、一个 slow Subscriber（2 ms delay）、
Queue depth 2、maximum-rate throughput，并分别对 `DROP_NEWEST`、`DROP_OLDEST` 和
`BLOCK_WITH_TIMEOUT`（1 ms）执行三次 repetition。实验报告 offered/delivered message、
p50/p90/p99、Drop、Overflow、Blocking 和 Throughput。Drop-heavy offered throughput 不等同于
完全 delivered throughput。

## 进程与资源模型

Python Runner 拥有精确的子进程 PID，并且只为自定义 Transport 启动 per-case Registry。它会
创建唯一 Topic、UDS path、ROS domain 和 Run directory，等待 Subscriber-ready marker，并要求
ROS Publisher 观察到预期 Subscription count。File-marker acknowledgement 使 CPU/RSS snapshot
与 Publisher measurement window 对齐。

所有 Readiness、Measurement、Child exit 和 Shutdown wait 都有上限。Cleanup 只向 Runner 的
精确子进程 PID 发送 Signal，只检查 Case 创建的 Socket path，并比较 Case 执行前后的项目 SHM
namespace。它从不以通配符删除 SHM，也不宽泛地杀死 ROS 进程。正常 Resource 仍由
Owner/Registry cleanup 负责。

以下情况会使运行无效：Subscriber 未 Ready、进程崩溃或非零退出、Measurement 太短、Artifact
缺失、Payload/Sequence corruption、Latency storage overflow，或 Test resource 残留。明确计数
的 Sequence gap 和 Policy drop 仍是有效结果。

## 结果

Raw data 只保存在本机，并被 Git 忽略：

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

`machine.json` 不记录 Hostname、Username 和 Home directory，但会记录 OS/Kernel/WSL、
Architecture、CPU、logical CPU count、total memory、Compiler、CMake、Python、Build type/flag、
Git revision/dirty state、ROS distro/prefix、RMW 和实际生效配置。per-run Summary 保留完整
正确性和进程退出状态。Aggregate file 为每个重复 Case identity 保存一个记录，包含
median/min/max 和指向三次运行的路径。

## 构建与运行

构建自定义 Release Benchmark：

```bash
cmake -S . -B .work/public/build_release \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DENABLE_ASAN=OFF \
  -DENABLE_UBSAN=OFF
cmake --build .work/public/build_release -j
```

构建 direct ROS2 Benchmark：

```bash
source /opt/ros/jazzy/setup.bash
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
colcon --log-base .work/public/ros2/log build \
  --base-paths benchmark/ros2 \
  --build-base .work/public/ros2/build \
  --install-base .work/public/ros2/install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
```

运行 Smoke matrix：

```bash
source /opt/ros/jazzy/setup.bash
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
python3 benchmark/python/run_benchmarks.py \
  --config benchmark/configs/smoke.json
```

运行一个精确主 Case（Filter 会禁用次要 Backpressure case）：

```bash
python3 benchmark/python/run_benchmarks.py \
  --config benchmark/configs/full.json \
  --transport shm_loan \
  --message-size 65536 \
  --subscribers 4 \
  --profile throughput \
  --repetitions 1
```

运行完整的 432-case 主矩阵和 9 次 Backpressure 运行：

```bash
python3 benchmark/python/run_benchmarks.py \
  --config benchmark/configs/full.json
```

不修改 Raw data，重新校验并生成 Aggregate file 和 Plot：

```bash
python3 benchmark/python/analyze_results.py benchmark/results/<run_id>
python3 benchmark/python/plot_results.py benchmark/results/<run_id>
```

使用 `--dry-run` 检查确定性的 Case expansion；只验证 Orchestration 时使用 `--no-plots`。
Filter 还支持指定单个 Transport、Message size、Topology 或 Profile。

## Profiling 证据

Profiling 运行复用相同 Release Endpoint、Payload、Queue setting、Timing 和 Runner。其代表性
自定义矩阵覆盖适用的 64 B、4 KiB、64 KiB、1 MiB、4 MiB 1-to-1，以及 64 KiB、4 MiB
1-to-4。direct ROS2 对照覆盖 64 KiB 和 4 MiB 1-to-1。聚焦的 SHM 优化前后验证针对 Latency
和 Throughput 各执行三次 repetition。

记录结果的 WSL2 主机没有安装 `perf` 或 `strace`。Profiling report 不声称获得 symbol
hotspot 或 syscall count。有界备用 observer 针对精确的 Benchmark 子进程，记录 measurement
window CPU tick、page fault、voluntary/nonvoluntary context switch 和 100 ms wait-channel
sample。复现命令为：

```bash
source /opt/ros/jazzy/setup.bash
source .work/public/profile/ros2/install/setup.bash
python3 benchmark/profiling/run_profile.py \
  --output-root .work/public/profile \
  --build-dir .work/public/profile/build \
  --ros-install .work/public/profile/ros2/install
```

精简 Profiling 证据位于 `benchmark/profiling/`。原始 Aggregate 保留在
`benchmark/results/reference_baseline/`，优化后的完整矩阵 Aggregate 单独保留在
`benchmark/results/reference_current/`。Profiling file 记录 Attribution、Copy Path analysis、
限制和 Acceptance evidence。

## 最终参考结果

主要的已提交 Reference 是 `benchmark/results/reference_current/`：Git `971129a` 上的
441/441 次有效运行和 147/147 个有效聚合组。测量环境为 WSL2、Intel i5-8300H、8 logical CPUs、
GCC 13.3、Release `-O3 -DNDEBUG -g -fno-omit-frame-pointer`、ROS2 Jazzy 和
`rmw_fastrtps_cpp`。`reference_baseline` 保留为优化前历史证据。

以下 1-to-1 median 将 fixed-rate p50 Latency 与 maximum-rate correct delivered Throughput
配对展示：

| Size | UDS p50 us / MiB/s | SHM Copy p50 us / MiB/s | SHM Loan p50 us / MiB/s | ROS2 p50 us / MiB/s |
| ---: | ---: | ---: | ---: | ---: |
| 64 B | 80.7 / 9.0 | 170.0 / 8.0 | 157.2 / 9.2 | 154.1 / 1.4 |
| 64 KiB | 97.6 / 1104.6 | 254.0 / 1070.5 | 240.4 / 1063.9 | 184.5 / 529.4 |
| 1 MiB | 742.7 / 1167.9 | 407.8 / 1405.5 | 287.7 / 1474.5 | 11205.7 / 920.1 |
| 4 MiB | 2053.4 / 1113.0 | 626.9 / 1500.0 | 274.1 / 1524.6 | 11945.9 / 947.0 |

本次运行中，64 KiB 及以下由 UDS 获得最低 p50。4 MiB 时，SHM Copy 和 Loan 的 delivered
Throughput 分别为 1500.0 和 1524.6 MiB/s，UDS 为 1113.0 MiB/s；与此同时，SHM mapped Pool
使 Publisher+Subscriber peak RSS 总和升至约 92.8 MiB，UDS 约为 16.3 MiB。4 MiB one-to-four 的
aggregate delivery 为 UDS 2562.6 MiB/s、Copy 4146.6 MiB/s、Loan 4224.7 MiB/s；Aggregate
fanout 不等于 Publisher logical throughput。聚焦的 slow-Subscriber 实验中，两种 Drop Policy
都记录到约 98.9% Drop；1 ms Block Policy delivered 452.7 messages/s，blocked time 为
4480.8 ms。

无需重跑矩阵即可重新生成精简、适合阅读的结果：

```bash
scripts/demo/demo_benchmark.sh
```

## 解释与限制

只在相同 configured rate、Size 和 Topology 下比较 Latency-profile p50/p99。尤其在 one-to-four
场景中，应分别比较 Publisher logical throughput 与 aggregate delivered throughput。判断
Throughput 时还应结合 CPU 和 Loss/Error counter，并使用报告的 min/max range 评估 repetition
variability。SHM Copy 与 Loan 的对比隔离了 Publisher payload preparation；SHM 与 UDS 的对比
还改变了 Kernel Socket payload transfer、Queue 和 Receive API。

结果描述单次 WSL/native Host session 和普通 OS scheduling，不是 hard real-time bound。实验
没有设置 CPU isolation、affinity、priority、cache conditioning 或 background-load control。
ROS2 `UInt8MultiArray` serialization 和 DDS Protocol overhead 位于相同 application byte count
之外。ROS QoS 和自定义 Backpressure 是相似的对比配置，不是等价保证。Throughput Latency
采用抽样，不是完整 tail distribution。Aggregate 本身无法把原因归结为 Syscall、Copy、Lock、
Scheduling 或 Cache behavior；单独的 Profiling 证据记录了可用的 Attribution 及其限制。
