# 优化前参考结果

这些精简 artifact 来自在 Git commit `cf353091f7149ac4f458ec77d98f23bb948155b2` 上执行的本机
完整运行 `benchmark/results/phase8_full_cf35309`。

- `machine.json`：已脱敏的主机、工具链、Git、ROS、RMW 和实际生效配置 metadata。
- `config.json`：432 个主 case 和 9 次 Backpressure 运行的完整确定性顺序。
- `summary.json`：147 个重复 case group 的 median/min/max 指标。
- `summary.csv`：供外部检查使用的扁平化聚合指标。
- 四个 PNG 文件：要求生成的 latency、throughput、CPU 和 Subscriber scaling 图表。

441 个逐次运行目录和原始 latency CSV 文件合计约 106 MiB，保留在被忽略的本机完整运行目录中，
未提交到仓库。任何原始测量都没有被编辑或过滤；`summary.json` 保存了指向每次 repetition 的
相对路径。
