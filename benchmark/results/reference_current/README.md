# 优化后参考结果

这些精简 artifact 来自在 Git commit `971129a4495fcd870efe05b51d2dfe8e0087a0a9` 上执行的完整
441-run 主矩阵与 Backpressure 矩阵（run id `phase8_1_full_971129a`）。

- `machine.json`：已脱敏的主机、工具链、Git、ROS 和实际生效配置 metadata。
- `config.json`：确定性的 441-case 顺序和 Benchmark 配置。
- `summary.json` 与 `summary.csv`：包含 repetition 最小值/最大值的聚合 median。
- 四个 PNG 文件：latency、throughput、CPU 和 Subscriber scaling 图表。

全部 441 次运行和 147 个聚合组均有效。原始逐次运行目录和 latency CSV 不提交到仓库；仓库只
保留经过校验的 aggregate、config、machine metadata 和图表。优化前的历史对照保存在
`reference_baseline/`。
