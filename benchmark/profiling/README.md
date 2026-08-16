# Profiling 证据

本目录保存性能分析和优化工作的精简证据，用于解释原始结果，同时避免提交原始 trace 或
逐次运行的 Benchmark 目录树。

## 方法

基线 profile 使用干净的 Git revision
`4caf333234c644fd7405db4fb93a9039939587ad`。优化后的完整矩阵使用干净的 revision
`971129a4495fcd870efe05b51d2dfe8e0087a0a9`。两次自定义构建均为 Release binary，编译参数为
`-O3 -DNDEBUG -g -fno-omit-frame-pointer`。

测量使用的 WSL2 主机没有安装 `perf` 和 `strace`，因此本文不声称获得了 perf counter、symbol
hotspot、syscall 次数或 syscall 时间占比。备用 observer 从 `/proc` 记录 measurement boundary
处的 CPU tick、fault 和 context switch，并以 100 ms 间隔采样现有 Benchmark runner 启动的
Publisher、Subscriber 和 Registry 的 `wchan`。

代表性矩阵覆盖自定义 UDS、SHM Copy 和 SHM Loan 的 latency/throughput profile；适用的消息大小
包括 64 B、4 KiB、64 KiB、1 MiB 和 4 MiB，并包含 64 KiB 与 4 MiB 的 1-to-4 fanout。direct
ROS2 对照覆盖 64 KiB 和 4 MiB 的 1-to-1。优化前后验证针对两条 SHM 路径，在 64 B、64 KiB、
1 MiB、4 MiB 1-to-1 以及 64 KiB、4 MiB 1-to-4 场景中，对 latency 和 throughput 各重复三次。

## 文件

- `phase8_1_summary.json`：工具状态、revision、24 组聚焦的优化前后结果、验证信息和优化来源。
- `before_after_summary.csv`：便于检查的扁平化聚焦指标。
- `hotspot_summary.csv`：所有已观察到的进程 counter 和最常采样到的 wait channel。文件名描述
  调查目标；这些记录不是 symbol-level CPU profile。
- `syscall_summary.csv`：工具可用性，以及在缺少 `strace` 时能够陈述的有限 syscall boundary 证据。
- `run_phase8_1_profile.py`：采集代表性原始 profile 的有界 observer。

完整的优化后 441-run 聚合位于 `benchmark/results/phase8_1_reference/`。优化前的历史结果保留在
`benchmark/results/phase8_reference/`。

## 复现

在专用工作目录中构建自定义和 direct ROS2 Release Endpoint，然后加载 ROS2 环境并执行：

```bash
source /opt/ros/jazzy/setup.bash
source .work/public/profile/ros2/install/setup.bash
python3 benchmark/profiling/run_phase8_1_profile.py \
  --output-root .work/public/profile \
  --build-dir .work/public/profile/build \
  --ros-install .work/public/profile/ros2/install
```

输出根目录必须尚不存在。Observer 使用 `benchmark/configs/full.json` 中的时间参数和 Queue
配置，每个 case 执行一次 profiling repetition；如果某次运行无效或被测子进程集合不精确，
脚本会直接失败。只有在仓库维护者批准后才安装和使用 `perf` 或 `strace`；不要把这些备用证据
标记为上述任一工具的输出。
