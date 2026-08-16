# 开发工作流

## 仓库规则

保持 Core 不依赖 ROS2，维持 Control Plane/Data Plane 分离，并要求在优化 transport 前提供
Benchmark 或 profiling 证据。不要声称整个仓库实现了 zero-copy。

构建、安装、日志、sanitizer、Demo 和原始结果统一输出到 `.work/`。永久源码、测试、文档和
已提交的精简 reference 证据保留在现有仓库目录中。

## Core 构建与测试

```bash
cmake -S . -B .work/local/build_debug -DCMAKE_BUILD_TYPE=Debug
cmake --build .work/local/build_debug -j
ctest --test-dir .work/local/build_debug --output-on-failure
```

项目代码使用 `-Wall -Wextra -Wpedantic` 编译。修改所有权、解析、Queue 或 IPC 时，应分别以
`-DENABLE_ASAN=ON` 和 `-DENABLE_UBSAN=ON` 配置并运行独立构建。

## 安装契约

```bash
cmake -S . -B .work/local/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build .work/local/build_release -j
cmake --install .work/local/build_release --prefix .work/local/install
cmake -S examples/external_consumer -B .work/local/external_consumer \
  -DCMAKE_PREFIX_PATH="$PWD/.work/local/install"
cmake --build .work/local/external_consumer -j
.work/local/external_consumer/mw_external_consumer
```

下游项目通过 `find_package(mw CONFIG REQUIRED)` 和 `mw::mw_core` 使用本项目。

## Demo 与文档

运行有界的非 ROS 场景和本地 Markdown link 校验：

```bash
MW_BUILD_DIR="$PWD/.work/local/build_release" scripts/demo/run_all_smoke.sh
python3 scripts/validate_markdown_links.py
```

ROS2 Adapter 是独立的 ament package，必须针对已安装的 Core 构建。详见
[ROS2 Adapter](ROS2_ADAPTER.md)。Benchmark 命令和证据边界见 [Benchmark](BENCHMARK.md)。

## 变更纪律

Commit 应目的单一，并保留工作树中无关的改动；根据变更触及的所有权和进程边界选择相应检查。
不要提交 `.work/`、原始 Benchmark 矩阵、coverage 输出、profiler 输出、构建目录或生成的 ROS
日志。除非仓库维护者明确要求，否则不要创建 tag 或 push。
