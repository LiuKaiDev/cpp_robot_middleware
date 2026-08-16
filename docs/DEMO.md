# 可复现 Demo

## 前置条件

在 Linux 上从仓库根目录运行。先在 `.work/public/` 下构建 Core Release binary：

```bash
cmake -S . -B .work/public/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build .work/public/build_release -j
```

脚本默认使用该构建目录；需要时可通过 `MW_BUILD_DIR=/absolute/path` 覆盖。每个脚本都使用唯一
名称、有界等待、精确的子进程 PID 和 EXIT cleanup trap。任何脚本都不会使用宽泛的 `pkill`
或通配符方式清理 `/dev/shm`。

## Demo 1：基础 Pub/Sub

```bash
scripts/demo/demo_basic_pubsub.sh
```

运行一个 Registry、一个 SHM Publisher 和一个 Subscriber，传输五条确定性的 64-byte 消息。
脚本会打印实时的 `mwctl` Node/Topic/Stats 状态，并验证发送/接收数量、sequence 和 payload
正确性。

## Demo 2：大消息

```bash
scripts/demo/demo_large_message.sh
```

通过普通 SHM Copy API 传输三条 4 MiB 消息，并逐字节验证 payload 和 sequence。这是正确性
Demo，不是新的 Benchmark；测量性能以已提交的优化后 reference 为准。

## Demo 3：多 Subscriber 共享

```bash
scripts/demo/demo_multi_subscriber.sh
```

运行一个 Publisher 和四个独立 Subscriber。四份输出必须报告相同的 `pool_id`、
`chunk_index`、`generation` 和 `payload_offset`，证明它们共享同一个逻辑 Chunk。
Demo 不打印也不声称虚拟指针相等。

## Demo 4：Backpressure

```bash
scripts/demo/demo_backpressure.sh
```

使用现有 Benchmark Endpoint、depth-2 Queue 和一个刻意放慢的 Subscriber。脚本依次执行
`DROP_NEWEST`、`DROP_OLDEST` 和 `BLOCK_WITH_TIMEOUT`，然后从生成的聚合结果中打印实际
delivered rate、drop/overflow 数量、blocked operation 和 blocked time。

## Demo 5：崩溃恢复

```bash
scripts/demo/demo_crash_recovery.sh
```

脚本启动一个 Subscriber，打印其活动 Registry record，只向该精确 PID 发送 `SIGKILL`，等待
记录被移除，然后在相同 Endpoint path 上启动 replacement 并验证新消息。更深入的 outstanding
view 和 blocked Producer 场景由集成测试覆盖。

## Demo 6：ROS2 Adapter

先安装 Core 并构建 Adapter：

```bash
cmake --install .work/public/build_release --prefix .work/public/install
source /opt/ros/jazzy/setup.bash
colcon --log-base .work/public/ros2/log build \
  --base-paths ros2_adapter \
  --build-base .work/public/ros2/build \
  --install-base .work/public/ros2/install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_PREFIX_PATH=$PWD/.work/public/install;/opt/ros/jazzy"
```

然后运行：

```bash
scripts/demo/demo_ros2_adapter.sh
```

脚本在 ROS2 input 上发送真实的 `std_msgs/msg/String`，依次经过 ROS2-to-middleware
serialization、原生 SHM Topic、middleware-to-ROS2 deserialization 和独立的 ROS2 output
Topic，并验证回传 payload。Twist 和 1280x720 RGB8 Image 由 Adapter 测试覆盖。

## Demo 7：Benchmark 证据

```bash
scripts/demo/demo_benchmark.sh
```

该脚本解析 `benchmark/results/phase8_1_reference/summary.json`，不会重跑完整矩阵。它会打印
选定的 p50 latency、throughput、message rate 和 Backpressure median，并列出四个已提交图表
的路径。

## 自动 Smoke

运行所有非 ROS Demo：

```bash
scripts/demo/run_all_smoke.sh
```

包含单独构建并加载环境的 ROS2 Demo：

```bash
scripts/demo/run_all_smoke.sh --with-ros2
```

## 实际录屏

一次真实成功的基础 Demo 记录保存在
[assets/demo/terminal_demo.txt](assets/demo/terminal_demo.txt)。安装 Pillow 后，可在不修改内容的
前提下渲染该 transcript：

```bash
python3 scripts/demo/render_terminal_gif.py \
  docs/assets/demo/terminal_demo.txt docs/assets/demo/demo.gif
```

![实际终端 Demo](assets/demo/demo.gif)
