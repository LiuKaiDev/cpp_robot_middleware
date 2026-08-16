# 优化后参考结果

这些精简 artifact 来自在 Git commit `971129a4495fcd870efe05b51d2dfe8e0087a0a9` 上执行的完整
441-run 主矩阵与 Backpressure 矩阵。

- `machine.json`：已脱敏的主机、工具链、Git、ROS 和实际生效配置 metadata。
- `config.json`：确定性的 441-case 顺序和 Benchmark 配置。
- `summary.json` 与 `summary.csv`：包含 repetition 最小值/最大值的聚合 median。
- 四个 PNG 文件：latency、throughput、CPU 和 Subscriber scaling 图表。

全部 441 次运行和 147 个聚合组均有效。验证期间，原始运行目录树存放在
`.work/phase_8_1/full_matrix/phase8_1_full_971129a`，未提交到仓库；保存这些精简 artifact 后，
清理步骤会删除该目录。原始 `phase8_reference/` 保持不变，用于历史对照。
