# Project Completion Checklist

The mandatory v1 scope is complete. Each PASS below is tied to current source, tests, or committed
measurement evidence rather than only historical development notes.

| Mandatory requirement | Status | Evidence |
| --- | --- | --- |
| C++17 Middleware Core | PASS | [`middleware/CMakeLists.txt`](../middleware/CMakeLists.txt), [Architecture](ARCHITECTURE.md) |
| Publisher API | PASS | [`publisher.hpp`](../middleware/include/mw/publisher.hpp) |
| Subscriber API | PASS | [`subscriber.hpp`](../middleware/include/mw/subscriber.hpp) |
| Unix Domain Socket baseline | PASS | [UDS baseline](UDS_BASELINE.md), process/integration tests |
| Registry daemon | PASS | [`registry/`](../registry/), [Control plane](CONTROL_PLANE.md) |
| Topic discovery | PASS | [Protocol](PROTOCOL.md), registry discovery tests |
| Node registration | PASS | [Protocol](PROTOCOL.md), registry state tests |
| `mwctl` | PASS | node list, topic list/info, stats; registry integration test |
| Shared Memory transport | PASS | [Data plane](DATA_PLANE.md), SHM transport tests |
| Shared-memory RAII | PASS | `SharedMemoryRegion` unit tests, [Memory model](MEMORY_MODEL.md) |
| Memory pool | PASS | [Memory pool](MEMORY_POOL.md), pool unit tests |
| Chunk lifecycle | PASS | [Message lifecycle](MESSAGE_LIFECYCLE.md), pool tests |
| Multi-subscriber | PASS | real 1-to-4 SHM and UDS integration tests |
| Reference counting | PASS | pool and multi-subscriber lifecycle tests |
| Subscriber queue | PASS | queue unit tests and long wraparound test |
| Backpressure | PASS | all three policies in unit/integration/benchmark evidence |
| Loaned Sample | PASS | move/cancel/publish/view tests, [Queues and loaning](QUEUES_AND_LOANING.md) |
| Runtime metrics | PASS | `PublishResult`, queue/pool instrumentation, `mwctl stats`, benchmark artifacts |
| Heartbeat | PASS | injectable liveness unit tests and real-process integration |
| Crash detection | PASS | subscriber/publisher `SIGKILL` tests |
| Resource cleanup | PASS | exact pool/queue/socket/reference recovery tests |
| ROS2 adapter | PASS | [ROS2 adapter](ROS2_ADAPTER.md), String/Twist/Image both directions |
| Automated benchmark | PASS | 441/441 optimized runs, [Benchmark](BENCHMARK.md) |
| Direct ROS2 baseline | PASS | ROS2 benchmark package and optimized reference |
| Unit tests | PASS | protocol, IPC, pool, queue, lifecycle, registry, benchmark units |
| Integration tests | PASS | cross-process, discovery, SHM, fanout, crash, ROS2 |
| Final README | PASS | [`README.md`](../README.md) |
| Architecture documentation | PASS | [Architecture](ARCHITECTURE.md) and Mermaid diagram |
| Reproducible demos | PASS | [Demo guide](DEMO.md) and [`scripts/demo/`](../scripts/demo/) |

## Optional/Future Items

The following are not mandatory v1 failures and are **NOT IMPLEMENTED**: lock-free/SPSC queue,
`eventfd`, `SCM_RIGHTS`, `memfd_create`, per-thread allocator cache, affinity/scheduler tuning,
PointCloud2, multi-publisher semantics, TCP/distributed transport, custom RMW, DDS/RTPS,
persistence, security, and registry restart recovery.

See [Known Limitations](KNOWN_LIMITATIONS.md) for the precise current boundary.
