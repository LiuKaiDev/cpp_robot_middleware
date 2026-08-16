# Start Here

`cpp_robot_middleware` is complete through the planned Phase 9 scope. It is a Linux/C++17 local
multi-process Pub/Sub middleware with separate UDS control and UDS/SHM data planes, bounded
multi-subscriber ownership, crash recovery, a separate ROS2 adapter, and reproducible benchmarks.

Start with [README.md](README.md) for the project overview, architecture, measured results, and
limitations. The shortest executable path is:

```bash
cmake -S . -B .work/phase_9/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build .work/phase_9/build_release -j
scripts/demo/demo_basic_pubsub.sh
```

Then use:

- [Demo guide](docs/DEMO.md) for seven bounded scenarios;
- [Architecture](docs/ARCHITECTURE.md) for process, thread, dependency, and ownership boundaries;
- [Protocol](docs/PROTOCOL.md), [memory model](docs/MEMORY_MODEL.md), and
  [message lifecycle](docs/MESSAGE_LIFECYCLE.md) for implementation details;
- [Benchmark](docs/BENCHMARK.md) for methodology and the optimized committed reference;
- [Known limitations](docs/KNOWN_LIMITATIONS.md) for claims the project intentionally does not make;
- [Interview guide](docs/INTERVIEW_GUIDE.md) and [resume guide](docs/RESUME.md) for portfolio use.

`PROJECT_PLAN.md` remains the source of truth for scope. Historical phase tasks and reports remain
under `CODEX_TASKS/` and `docs/reports/` as engineering evidence; they are not the primary reader
path.
