# C++ 高性能发布订阅通信中间件与 ROS2 适配框架

## 项目规划书

> **项目定位：** 面向机器人单机多进程通信场景，自主实现一个基于 Linux + C++17 的高性能发布订阅通信中间件。系统以 Unix Domain Socket 构建控制面，以 Shared Memory 构建高吞吐数据面，自主实现 Topic 注册发现、共享内存管理、内存池、消息生命周期、发布订阅队列、背压控制、进程存活检测和性能监控，并通过 ROS2 Adapter 接入 ROS2 生态。
>
> **核心目标：不是“重新实现 ROS2 / DDS”，也不是简单调用现有共享内存库，而是自主实现一个边界清晰、可解释、可测试、可 Benchmark 的 Mini Pub/Sub Middleware，理解机器人通信系统中的 IPC、共享内存、并发、内存管理和性能优化问题。**
>
> 最终项目应形成：
>
> **C++ 系统能力 + Linux IPC + 并发编程 + 内存管理 + 中间件设计 + ROS2 集成 + 性能分析**
>
> 的完整能力闭环。

---

# 1. 项目背景

机器人软件通常由多个独立进程共同组成，例如：

```text
Camera Driver
      ↓
Perception
      ↓
Localization
      ↓
Planning
      ↓
Control
```

不同模块之间需要持续交换：

```text
控制指令
状态信息
IMU
LaserScan
Camera Image
PointCloud
地图
轨迹
```

其中既有几十字节的小控制消息，也可能有数 MB 的图像和点云。

传统进程间通信路径可能涉及：

```text
Application Buffer
       ↓
Serialization
       ↓
Kernel Buffer
       ↓
Transport
       ↓
Kernel Buffer
       ↓
Deserialization
       ↓
Application Buffer
```

对于高频、大数据量机器人消息，通信开销可能来自：

- 数据复制；
- 内存分配；
- 序列化；
- 系统调用；
- 线程切换；
- 队列拥塞；
- 慢订阅者；
- 缓冲区耗尽；
- 进程异常退出；
- 调度抖动。

本项目希望从零实现一个简化但完整的 Pub/Sub 中间件，在单机多进程场景下重点研究：

```text
Control Plane
+
Shared Memory Data Plane
+
Memory Management
+
Concurrency
+
Backpressure
+
Fault Handling
+
Benchmark
```

---

# 2. 与已有项目的区别

已有 ROS2 AMR 项目核心解决的是：

> **机器人如何进行导航、控制、TF 管理、安全仲裁与系统集成。**

主要能力：

```text
ROS2
Nav2
AMCL
TF
底盘控制
速度仲裁
安全控制
Gazebo
```

本项目核心解决的是：

> **机器人不同软件模块之间的数据到底如何高效、可靠地流动。**

重点转向：

```text
Linux IPC
Shared Memory
Unix Domain Socket
Memory Pool
Ring Buffer
Atomic
Message Lifecycle
Backpressure
Discovery
Process Liveness
Benchmark
```

因此两个机器人项目形成：

```text
ROS2 AMR
    │
    └── 会使用机器人软件基础设施
              ↓
Middleware
    │
    └── 理解并实现机器人软件基础设施
```

---

# 3. 与 Linux 弱网项目、Raft 项目的区别

Linux 弱网项目中的 D-Bus 主要体现：

> 使用现有 IPC 框架构建 Linux 服务。

Raft 项目主要体现：

> 网络 RPC、分布式一致性、持久化和故障恢复。

本项目则重点体现：

> **自主设计本机高性能 IPC 与发布订阅基础设施。**

因此：

```text
Linux WeakNet
    ↓
会使用 IPC

Raft KV
    ↓
会设计分布式通信系统

Middleware
    ↓
会实现 IPC / Middleware Core
```

三者不是简单重复，而是能力递进。

---

# 4. 项目最终目标

系统最终需要完成：

1. 独立于 ROS2 的 C++17 Middleware Core；
2. Publisher / Subscriber API；
3. Topic 注册、发现与查询；
4. Registry Daemon；
5. Unix Domain Socket 基线传输；
6. Shared Memory 数据传输；
7. 可复用 Memory Pool；
8. 多订阅者共享同一 Payload；
9. 消息生命周期与引用计数；
10. Subscriber Queue；
11. Queue Overflow / Backpressure；
12. Loaned Sample API；
13. 进程心跳与异常退出检测；
14. 资源回收；
15. CLI 调试工具；
16. Runtime Metrics；
17. ROS2 Adapter；
18. 自动 Benchmark；
19. UDS / SHM / ROS2 Baseline 定量比较；
20. 完整 README / Architecture / Benchmark 文档。

---

# 5. 项目设计原则

整个项目必须遵循以下原则。

## 5.1 Middleware Core 不依赖 ROS2

核心结构：

```text
Middleware Core
       ↑
       │
 ROS2 Adapter
```

禁止：

```text
Middleware Core
       ↓
依赖 rclcpp
```

ROS2 只是外部适配层。

---

## 5.2 Control Plane 与 Data Plane 分离

控制面负责：

```text
注册
发现
Topic 信息
进程信息
资源协调
心跳
```

数据面负责：

```text
消息传输
Queue
Shared Memory
Memory Pool
```

结构：

```text
            Registry Daemon
             Control Plane
                  │
         Unix Domain Socket
          /             \
Publisher                 Subscriber
    │                         │
    └──── Shared Memory ──────┘
             Data Plane
```

---

## 5.3 第一版优先正确性，不盲目追求 Lock-Free

开发顺序：

```text
正确
 ↓
可测试
 ↓
可测量
 ↓
Profile
 ↓
再优化
```

禁止一开始大量使用复杂 lock-free 数据结构。

只有 Benchmark 或 profiling 证明某处确实存在瓶颈后，才针对性优化。

---

## 5.4 不虚假宣传 Zero-Copy

项目区分三种模式：

### Copy Transport

```text
Application
    ↓ copy
Middleware Buffer
```

### Reduced-Copy Shared Memory

```text
Application
    ↓ copy
Shared Memory
    ↓
Subscriber Direct Read
```

### Loaned Sample

```text
Shared Memory Buffer
       ↑
Publisher 直接填写
       ↓
Subscriber 直接读取
```

只有最后一种在完整链路确实没有 Payload Copy 时，才可以称：

> Zero-Copy Data Path

否则必须写：

> Shared-Memory Transport / Reduced-Copy Transport

---

# 6. 项目边界

项目主要面向：

> **Linux 单机、多进程机器人软件通信。**

第一版不解决跨机器分布式通信。

第一版重点：

```text
Local IPC
Shared Memory
Pub/Sub
Large Message
Performance
```

而不是：

```text
Distributed Middleware
```

---

# 7. 推荐总体架构

```text
                           +----------------------+
                           |     mw_registryd     |
                           |                      |
                           | Node Registry        |
                           | Topic Registry       |
                           | Endpoint Discovery   |
                           | Heartbeat            |
                           | Resource Lifecycle   |
                           +----------+-----------+
                                      |
                              Control Plane
                             Unix Domain Socket
                                      |
               +----------------------+----------------------+
               |                                             |
               v                                             v
      +-------------------+                         +-------------------+
      |     Publisher     |                         |    Subscriber     |
      |                   |                         |                   |
      | Publisher API     |                         | Subscriber API    |
      | Loan API          |                         | Take API          |
      +---------+---------+                         +---------+---------+
                |                                             ^
                |                                             |
                |             Data Plane                      |
                +----------- Shared Memory -------------------+
                              |
                       +------+------+
                       | Memory Pool |
                       +------+------+
                              |
                       +------+------+
                       | Chunk Pool  |
                       +------+------+
                              |
                  +-----------+-----------+
                  |                       |
             Subscriber 1 Queue      Subscriber 2 Queue
```

---

# 8. 核心进程设计

至少包含以下程序。

## 8.1 mw_registryd

Middleware Registry Daemon。

负责：

```text
Node Registration
Topic Advertisement
Topic Subscription
Endpoint Discovery
Heartbeat
Resource Ownership
Status Query
Cleanup
```

---

## 8.2 Publisher Application

通过 Middleware API：

```cpp
Context context("camera_node");

auto publisher =
    context.createPublisher("/camera/image", config);
```

然后：

```cpp
publisher.publish(data, size);
```

或者：

```cpp
auto sample = publisher.loan(size);

fill(sample.data());

sample.publish();
```

---

## 8.3 Subscriber Application

```cpp
Context context("perception_node");

auto subscriber =
    context.createSubscriber("/camera/image", config);
```

读取：

```cpp
auto sample = subscriber.take();

process(sample.data(), sample.size());
```

Sample 生命周期使用 RAII 管理。

---

## 8.4 mwctl

类似：

```text
ros2 node list
ros2 topic list
```

但只面向本项目。

支持：

```bash
mwctl node list

mwctl topic list

mwctl topic info /camera/image

mwctl stats

mwctl endpoint list
```

---

# 9. Public API 设计

第一版建议 API：

```cpp
class Context {
public:
    explicit Context(std::string node_name);

    Publisher createPublisher(
        const std::string& topic,
        const PublisherConfig& config);

    Subscriber createSubscriber(
        const std::string& topic,
        const SubscriberConfig& config);
};
```

Publisher：

```cpp
class Publisher {
public:
    PublishResult publish(
        const void* data,
        std::size_t size);

    LoanedSample loan(
        std::size_t size);

    PublisherStats stats() const;
};
```

Subscriber：

```cpp
class Subscriber {
public:
    std::optional<SampleView> take();

    std::optional<SampleView> waitAndTake(
        std::chrono::milliseconds timeout);

    SubscriberStats stats() const;
};
```

---

# 10. Topic 模型

每个 Topic 至少维护：

```text
topic_id
topic_name
type_name
type_hash
max_message_size
queue_depth
overflow_policy
transport_type
publisher_count
subscriber_count
```

示例：

```text
Topic:
    id: 17
    name: /camera/image
    type: sensor_msgs/Image
    max_size: 4194304
    depth: 8
    transport: SHM
```

---

# 11. Type Compatibility

虽然 Middleware Core 不需要理解具体消息字段，但必须防止：

```text
Publisher:
    /imu
    ImuMessage

Subscriber:
    /imu
    ImageMessage
```

错误连接。

因此注册时使用：

```text
type_name
+
type_hash
```

Registry 检查双方是否兼容。

第一版不设计完整 IDL / Schema 系统。

---

# 12. Control Plane 协议

Registry 与 Client 使用 Unix Domain Socket。

建议协议：

```text
ControlHeader
+
Payload
```

例如：

```cpp
struct ControlHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t opcode;
    uint32_t request_id;
    uint32_t payload_size;
};
```

Opcode：

```text
REGISTER_NODE
UNREGISTER_NODE

ADVERTISE_TOPIC
UNADVERTISE_TOPIC

SUBSCRIBE_TOPIC
UNSUBSCRIBE_TOPIC

HEARTBEAT

QUERY_NODE
QUERY_TOPIC
QUERY_STATS
```

控制协议强调：

```text
正确
清晰
可调试
有版本号
```

而不是极致性能。

---

# 13. Socket Baseline Transport

第一版首先实现一个最简单但完整的数据通路：

```text
Publisher
   ↓
Unix Domain Socket
   ↓
Subscriber
```

使用：

```text
Length-Prefixed Frame
```

帧结构：

```text
FrameHeader
+
Payload
```

例如：

```cpp
struct FrameHeader {
    uint32_t magic;
    uint32_t payload_size;
    uint64_t sequence;
    uint64_t timestamp_ns;
};
```

这个 Transport 用于：

1. 验证 Publisher / Subscriber API；
2. 建立可靠的功能 Baseline；
3. 后期与 SHM 做 Benchmark。

---

# 14. Shared Memory Transport

第二条核心数据通路：

```text
Publisher
      ↓
Shared Memory
      ↓
Subscriber
```

第一版建议：

```text
shm_open
+
ftruncate
+
mmap
```

封装：

```cpp
class SharedMemoryRegion {
public:
    static SharedMemoryRegion create(...);
    static SharedMemoryRegion open(...);

    void* data();
    std::size_t size() const;

private:
    UniqueFd fd_;
    void* addr_;
};
```

必须使用 RAII 管理：

```text
fd
mmap
munmap
close
```

避免业务代码直接散落：

```cpp
open()
mmap()
close()
```

---

# 15. Shared Memory Layout

共享内存建议划分：

```text
+----------------------------------+
| Segment Header                   |
+----------------------------------+
| Global Metadata                  |
+----------------------------------+
| Chunk Pool Metadata              |
+----------------------------------+
| Subscriber Queue Metadata        |
+----------------------------------+
| Chunk 0                          |
+----------------------------------+
| Chunk 1                          |
+----------------------------------+
| Chunk 2                          |
+----------------------------------+
| ...                              |
+----------------------------------+
```

Segment Header：

```text
magic
version
segment_size
topic_id
chunk_count
creation_timestamp
owner_pid
```

---

# 16. Chunk 设计

每一块 Payload 对应：

```text
Chunk Header
+
Payload
```

建议 Chunk Header：

```cpp
struct ChunkHeader {
    std::atomic<uint32_t> ref_count;

    uint32_t payload_size;
    uint32_t capacity;

    uint64_t sequence;
    uint64_t publish_timestamp_ns;

    uint32_t topic_id;
    uint32_t flags;
};
```

后续可增加：

```text
source_timestamp
publisher_id
checksum
```

第一版不需要全部实现。

---

# 17. Message Lifecycle

这是项目最核心的部分之一。

消息生命周期：

```text
FREE
 ↓
LOANED
 ↓
PUBLISHED
 ↓
DELIVERED
 ↓
RELEASED
 ↓
FREE
```

Publisher Loan：

```text
Memory Pool
   ↓
allocate chunk
   ↓
LOANED
```

Publish：

```text
LOANED
   ↓
PUBLISHED
   ↓
enqueue to subscribers
```

Subscriber：

```text
take
 ↓
read
 ↓
SampleView destructor
 ↓
release
```

最后：

```text
ref_count == 0
       ↓
return to Memory Pool
```

---

# 18. 多 Subscriber 数据共享

例如：

```text
               Payload
                  │
       +----------+----------+
       │          │          │
   Subscriber1 Subscriber2 Subscriber3
```

禁止：

```text
Payload copy × 3
```

应该：

```text
1 Chunk
+
3 Chunk Handles
```

Publish 时：

```text
ref_count = successfully_enqueued_subscribers
```

每个 Subscriber 完成消费：

```text
ref_count--
```

最终：

```text
ref_count == 0
```

回收。

---

# 19. Memory Pool

高频通信路径中避免：

```cpp
new
delete
malloc
free
```

反复执行。

采用预分配 Memory Pool。

推荐 Size Class：

```text
256 B
4 KB
64 KB
1 MB
4 MB
```

具体数量通过 YAML / config 调节。

例如：

```yaml
memory_pool:

  - chunk_size: 256
    count: 4096

  - chunk_size: 4096
    count: 2048

  - chunk_size: 65536
    count: 512

  - chunk_size: 1048576
    count: 64

  - chunk_size: 4194304
    count: 16
```

当请求：

```text
1000 B
```

选择：

```text
4 KB Chunk
```

---

# 20. Memory Pool 第一版策略

第一版不强制 Lock-Free Allocator。

可以：

```text
Free List
+
Mutex
```

先保证：

```text
Correctness
```

之后通过 Profiling 判断：

```text
Allocator Lock
```

是否为瓶颈。

高级版本才考虑：

```text
Atomic Free List
Per-thread Cache
Lock-Free Pool
```

---

# 21. Subscriber Queue

Subscriber 不直接保存 Payload。

Queue 保存：

```text
ChunkHandle
```

例如：

```cpp
struct ChunkHandle {
    uint32_t pool_id;
    uint32_t chunk_index;
};
```

Subscriber Queue：

```text
ChunkHandle
ChunkHandle
ChunkHandle
...
```

从而避免重复复制 Payload。

---

# 22. Ring Buffer

推荐每个 Publisher → Subscriber Endpoint 使用固定容量 Ring Buffer。

第一版：

```text
Mutex
+
Ring Buffer
```

验证正确性。

第二版：

> 如果数据通路结构允许形成明确 SPSC 场景，

再实现：

```text
Atomic head
Atomic tail
```

SPSC Ring Buffer。

需要重点测试：

```text
wrap around
full
empty
producer faster
consumer faster
long duration
sequence correctness
```

---

# 23. Backpressure

Subscriber 处理速度可能低于 Publisher。

例如：

```text
Camera 30 FPS
     ↓
Perception 10 FPS
```

必须定义 Queue 满后的行为。

支持：

```text
DROP_NEWEST
DROP_OLDEST
BLOCK
```

配置：

```yaml
queue:
  depth: 8
  overflow_policy: DROP_OLDEST
```

DROP_NEWEST：

```text
queue full
   ↓
new message rejected
```

DROP_OLDEST：

```text
queue full
   ↓
release oldest
   ↓
enqueue newest
```

BLOCK：

```text
queue full
   ↓
publisher wait
```

必须支持：

```text
block timeout
```

防止永久死锁。

---

# 24. Loaned Sample API

高性能路径最终实现：

```cpp
auto sample = publisher.loan(1024 * 1024);

fill_camera_image(sample.data());

sample.publish();
```

而不是：

```cpp
std::vector<uint8_t> image;

fill(image);

publisher.publish(image.data(), image.size());
```

前者的数据直接填写在 Shared Memory Chunk 中。

---

# 25. LoanedSample 生命周期

LoanedSample 必须是：

```text
Move-only
RAII
```

禁止 Copy。

例如：

```cpp
class LoanedSample {
public:
    LoanedSample(const LoanedSample&) = delete;
    LoanedSample& operator=(
        const LoanedSample&) = delete;

    LoanedSample(LoanedSample&&) noexcept;

    void* data();

    void publish();

    ~LoanedSample();
};
```

如果：

```text
loan
 ↓
没有 publish
 ↓
destructor
```

则自动：

```text
return chunk
```

避免资源泄漏。

---

# 26. SampleView

Subscriber 读取：

```cpp
auto sample = subscriber.take();
```

SampleView：

```text
Read Only
+
RAII
```

析构：

```text
ref_count--
```

不允许 Subscriber 修改共享 Payload。

---

# 27. Notification 第一版

Shared Memory 只解决：

```text
数据在哪里
```

还需要解决：

> Subscriber 如何知道有新消息？

第一版本可以：

```text
Payload
   ↓
Shared Memory

Notification
   ↓
Unix Domain Socket
```

Publisher 发布：

```text
write payload
 ↓
enqueue handle
 ↓
send notification
```

这样先保证系统完整。

---

# 28. Notification 高级优化

后续升级：

```text
Shared Ring Buffer
+
eventfd
```

每个 Subscriber Endpoint 有：

```text
eventfd
```

Publisher：

```text
enqueue
 ↓
eventfd_write()
```

Subscriber：

```text
poll / epoll
 ↓
eventfd readable
 ↓
drain queue
```

如果实现 fd 动态传递，可由 Registry 通过：

```text
SCM_RIGHTS
```

完成描述符传递。

这一功能属于高级但推荐完成项。

---

# 29. Thread Model

目标：

> 数据路径尽可能不依赖额外 Worker Thread。

Publisher：

```text
Application Thread
      ↓
publish()
```

Subscriber：

```text
Application Thread
      ↓
waitAndTake()
```

Registry：

```text
Single Event Loop
+
epoll
```

处理：

```text
client connection
control message
heartbeat
query
```

避免无意义的：

```text
one connection = one thread
```

---

# 30. Node / Endpoint 模型

Node：

```text
camera_driver
perception
navigation
controller
```

Endpoint：

```text
PublisherEndpoint
SubscriberEndpoint
```

Registry：

```text
Node
 ├─ Publisher 1
 ├─ Publisher 2
 └─ Subscriber 1
```

每个对象分配：

```text
node_id
endpoint_id
topic_id
```

---

# 31. Heartbeat

每个客户端周期发送：

```text
HEARTBEAT
```

Registry 维护：

```text
last_seen_timestamp
```

超过阈值：

```text
ALIVE
 ↓
SUSPECTED
 ↓
DEAD
```

Registry 执行：

```text
remove endpoint
release ownership
cleanup stale resource
update topic state
```

---

# 32. Process Crash 场景

必须测试：

```text
Publisher SIGKILL

Subscriber SIGKILL

Publisher 正常退出

Subscriber 正常退出
```

系统不能要求：

```text
必须手工删除 /dev/shm 文件
```

才能继续运行。

需要考虑：

```text
stale shm
stale endpoint
unfinished loan
queued chunk
outstanding chunk
```

---

# 33. Outstanding Sample Tracking

Subscriber 如果：

```text
take()
 ↓
process crashes
```

则该 Chunk 可能永远：

```text
ref_count > 0
```

高级鲁棒版本应维护：

```text
Subscriber Outstanding Table
```

例如固定容量：

```text
outstanding[64]
```

Registry 检测 Subscriber 死亡后：

```text
scan outstanding handles
 ↓
release references
```

第一版如果暂未实现，必须明确记录为 Known Limitation。

---

# 34. Memory Exhaustion

Memory Pool 可能耗尽。

场景：

```text
Publisher rate high
Subscriber slow
Queue large
Subscriber keeps Sample
```

必须支持：

```text
AllocationFailure
```

并统计：

```text
allocation_failure_count
```

不能：

```text
nullptr dereference
crash
silent data corruption
```

策略：

```text
RETURN_ERROR
DROP
BLOCK_WITH_TIMEOUT
```

---

# 35. Runtime Metrics

Publisher 至少记录：

```text
published_messages
published_bytes
publish_failures
allocation_failures
dropped_messages
blocked_count
blocked_time_ns
```

Subscriber：

```text
received_messages
received_bytes
queue_overflow
sequence_gap
max_queue_depth
```

Registry：

```text
node_count
topic_count
endpoint_count
dead_node_count
```

---

# 36. mwctl

支持：

```bash
mwctl node list
```

示例：

```text
camera_node
perception_node
planner_node
```

支持：

```bash
mwctl topic list
```

例如：

```text
/camera/image
/imu/data
/cmd_vel
```

支持：

```bash
mwctl topic info /camera/image
```

输出：

```text
Topic ID: 8
Type: sensor_msgs/Image
Transport: SHM
Publishers: 1
Subscribers: 2
Queue Depth: 8
Message Size Limit: 4 MB
```

支持：

```bash
mwctl stats /camera/image
```

---

# 37. ROS2 Adapter

Middleware Core 完成之后增加：

```text
mw_ros2_adapter
```

整体：

```text
ROS2 Topic
    ↓
ROS2 Bridge
    ↓
Custom Middleware
```

以及：

```text
Custom Middleware
    ↓
ROS2 Bridge
    ↓
ROS2 Topic
```

---

# 38. ROS2 Adapter 第一版支持类型

优先：

```text
std_msgs/msg/String
geometry_msgs/msg/Twist
sensor_msgs/msg/Image
```

原因：

```text
String
  ↓
小消息

Twist
  ↓
机器人控制消息

Image
  ↓
大消息
```

三种类型即可体现不同通信场景。

PointCloud2 可作为高级扩展。

---

# 39. ROS2 Bridge 结构

例如：

```text
ros2_to_mw_bridge
```

执行：

```text
ROS Subscriber
      ↓
Serialization / Payload Adapter
      ↓
Middleware Publisher
```

反方向：

```text
mw_to_ros2_bridge
```

执行：

```text
Middleware Subscriber
       ↓
Message Decode
       ↓
ROS Publisher
```

必须明确：

> ROS2 Adapter 中存在的 serialization / copy 不应被算入 Middleware Core 的 Zero-Copy 宣传。

---

# 40. ROS2 演示场景

Demo：

```text
Synthetic Camera Publisher
        ↓
Custom Middleware SHM
        ↓
ROS2 Adapter
        ↓
ROS2 Image Topic
        ↓
ROS2 Subscriber / Viewer
```

证明：

> 自研 Middleware 能与 ROS2 软件栈协同工作。

---

# 41. Benchmark 目标

Benchmark 是整个项目最重要的部分之一。

没有 Benchmark：

> 不能称为高性能中间件项目。

必须定量回答：

```text
SHM 是否真的比 Socket 快？

什么时候快？

什么时候没有优势？

大消息优势有多明显？

CPU 使用率如何？

Subscriber 数量增加后如何变化？

Loaned Sample 有多大收益？

Backpressure 如何影响延迟？
```

---

# 42. Message Size Matrix

至少：

```text
64 B
1 KB
4 KB
64 KB
1 MB
4 MB
```

可选：

```text
8 MB
```

---

# 43. Benchmark Transport Matrix

必须比较：

### Baseline 1

```text
Custom UDS Transport
```

### Baseline 2

```text
Custom SHM Copy
```

### Proposed

```text
Custom SHM Loaned
```

### External Baseline

```text
ROS2 Jazzy
+
rmw_fastrtps_cpp
+
Cross Process
```

可选高级实验：

```text
ROS2 intra-process
```

或者：

```text
Fast DDS optimized shared-memory configuration
```

但不作为第一版必须功能。

---

# 44. Benchmark Topology

## 44.1 1 Publisher → 1 Subscriber

```text
P
↓
S
```

## 44.2 1 Publisher → 2 Subscribers

```text
      ┌→ S1
P ────┤
      └→ S2
```

## 44.3 1 Publisher → 4 Subscribers

```text
        S1
        ↑
S4 ←── P ──→ S2
        ↓
        S3
```

观察：

> 多 Subscriber 场景下 SHM Payload Sharing 的效果。

---

# 45. Benchmark 指标

至少记录：

```text
Latency p50
Latency p90
Latency p99

Throughput MB/s
Messages/s

Publisher CPU
Subscriber CPU

RSS Memory

Drop Rate
Queue Overflow

Allocation Failure

Blocked Time
```

高级：

```text
Context Switch
Page Fault
System Call Count
Cache Miss
```

---

# 46. Latency 测量

Publisher 写：

```text
publish_timestamp
```

Subscriber：

```text
receive_timestamp
```

计算：

```text
latency =
receive_timestamp
-
publish_timestamp
```

同机实验统一使用：

```text
monotonic clock
```

避免系统时间调整影响。

---

# 47. Benchmark 可重复性

每次实验包含：

```text
warmup
measurement
cooldown
```

参数固定。

结果输出：

```text
results/
    uds/
    shm_copy/
    shm_loan/
    ros2/
```

每组：

```text
raw_latency.csv
summary.json
cpu.csv
memory.csv
latency_histogram.png
throughput.png
```

---

# 48. Benchmark 成功标准

项目目标不是：

> **必须打败 Fast DDS。**

而是：

1. 能建立公平 Benchmark；
2. 能解释不同方案性能差异；
3. 能找到系统瓶颈；
4. 能说明 Shared Memory 的优势场景；
5. 能说明它的代价；
6. 能解释自己的设计局限。

即使：

```text
Custom Middleware
<
Fast DDS
```

只要分析合理，依然具有项目价值。

---

# 49. Fault Test Matrix

至少包含：

| Experiment | Slow Subscriber | Publisher Crash | Subscriber Crash | Queue Full | Memory Exhaustion |
|---|---:|---:|---:|---:|---:|
| Normal |  |  |  |  |  |
| Slow Consumer | ✓ |  |  | ✓ |  |
| Publisher Kill |  | ✓ |  |  |  |
| Subscriber Kill |  |  | ✓ |  |  |
| Queue Overflow | ✓ |  |  | ✓ |  |
| Pool Exhaustion | ✓ |  |  | ✓ | ✓ |

每种场景必须：

```text
可配置
可复现
可记录
可验证
```

---

# 50. Repository 规划

推荐：

```text
cpp_robot_middleware/
│
├── PROJECT_PLAN.md
├── README.md
├── CMakeLists.txt
│
├── cmake/
│
├── middleware/
│   ├── include/
│   │   └── mw/
│   │       ├── context.hpp
│   │       ├── publisher.hpp
│   │       ├── subscriber.hpp
│   │       ├── loaned_sample.hpp
│   │       ├── sample_view.hpp
│   │       └── config.hpp
│   │
│   └── src/
│
├── ipc/
│   ├── include/
│   └── src/
│       ├── unix_socket.cpp
│       ├── shared_memory.cpp
│       ├── memory_pool.cpp
│       ├── ring_buffer.cpp
│       └── event_notifier.cpp
│
├── registry/
│   ├── include/
│   └── src/
│       ├── registry.cpp
│       ├── registry_server.cpp
│       └── main.cpp
│
├── protocol/
│   ├── control_protocol.hpp
│   └── frame_protocol.hpp
│
├── tools/
│   └── mwctl/
│
├── examples/
│   ├── ping_publisher/
│   ├── ping_subscriber/
│   ├── image_publisher/
│   └── multi_subscriber/
│
├── benchmark/
│   ├── cpp/
│   ├── python/
│   ├── configs/
│   └── results/
│
├── tests/
│   ├── unit/
│   └── integration/
│
├── ros2_ws/
│   └── src/
│       └── mw_ros2_bridge/
│
├── config/
│   └── middleware.yaml
│
├── scripts/
│
└── docs/
    ├── ARCHITECTURE.md
    ├── CONTROL_PLANE.md
    ├── DATA_PLANE.md
    ├── MEMORY_MODEL.md
    ├── MESSAGE_LIFECYCLE.md
    ├── FAILURE_MODEL.md
    ├── BENCHMARK.md
    └── ROS2_ADAPTER.md
```

---

# 51. Library Packaging

Middleware Core 最终应生成：

```text
libmw_core.so
```

同时提供：

```text
include/mw/
```

支持外部程序：

```cmake
find_package(...)
target_link_libraries(...)
```

或者通过：

```text
CMake install/export
```

完成安装。

这部分用于体现真正的：

> C++ Library Engineering

而不是所有代码都写成 ROS2 Node。

---

# 52. Configuration

核心参数放：

```yaml
registry:

  socket_path: /tmp/mw_registry.sock

  heartbeat_interval_ms: 500
  heartbeat_timeout_ms: 2000


transport:

  default: shared_memory


memory_pool:

  - chunk_size: 4096
    count: 2048

  - chunk_size: 65536
    count: 512

  - chunk_size: 1048576
    count: 64


queue:

  default_depth: 8

  overflow_policy: DROP_OLDEST
```

---

# 53. 项目开发阶段总览

```text
Phase 0
项目骨架 / Library Packaging
        ↓
Phase 1
Unix Domain Socket Pub/Sub Baseline
        ↓
Phase 2
Registry / Discovery / mwctl
        ↓
Phase 3
Shared Memory Data Plane V1
        ↓
Phase 4
Memory Pool / Chunk Lifecycle / Multi Subscriber
        ↓
Phase 5
Ring Buffer / Backpressure / Loaned Sample
        ↓
Phase 6
Heartbeat / Crash Recovery / Resource Cleanup
        ↓
Phase 7
ROS2 Adapter
        ↓
Phase 8
Benchmark / Profiling / Performance Optimization
        ↓
Phase 9
Documentation / Demo / Resume
```

---

# 54. Phase 0：项目骨架

## 目标

建立独立 C++17 工程。

完成：

```text
repository
CMake
library target
test target
install/export
clang-format
README skeleton
PROJECT_PLAN.md
```

推荐目录先只创建当前阶段需要的文件。

不要一次性生成全部空文件。

## 验收

```bash
cmake -S . -B build

cmake --build build

ctest --test-dir build
```

全部通过。

外部 example 可以成功：

```text
include middleware headers
+
link libmw_core
```

---

# 55. Phase 1：Unix Domain Socket Pub/Sub

## 目标

实现：

```text
Publisher
 ↓
UDS
 ↓
Subscriber
```

暂时：

```text
1 Publisher
1 Subscriber
1 Topic
```

实现：

```text
Context
Publisher
Subscriber
Frame Header
Message Sequence
Timestamp
```

支持：

```text
small message
large message
```

## 测试

至少：

```text
empty payload
64 B
1 KB
1 MB
invalid frame
disconnect
reconnect
partial read
```

## 验收

能够运行：

```bash
./mw_ping_subscriber

./mw_ping_publisher
```

连续发送至少：

```text
100000 messages
```

保证：

```text
sequence correct
payload correct
no crash
```

---

# 56. Phase 2：Registry 与 Discovery

## 目标

从：

```text
hardcoded connection
```

升级为：

```text
registry-based discovery
```

实现：

```text
mw_registryd
```

Publisher：

```text
register node
advertise topic
```

Subscriber：

```text
register node
subscribe topic
```

Registry 自动匹配双方。

实现：

```text
mwctl node list
mwctl topic list
mwctl topic info
```

## 验收

先启动：

```bash
mw_registryd
```

再任意顺序启动：

```text
Publisher
Subscriber
```

均可以建立通信。

---

# 57. Phase 3：Shared Memory Data Plane V1

## 目标

实现：

```text
Control
  ↓
UDS

Payload
  ↓
Shared Memory
```

Shared Memory 第一版：

```text
shm_open
ftruncate
mmap
```

消息通知暂时仍走：

```text
UDS
```

Notification 中只传：

```text
Chunk Handle
```

而不传 Payload。

## 验收

同一消息：

```text
UDS baseline
```

与：

```text
SHM transport
```

Payload 一致。

支持：

```text
1 KB
64 KB
1 MB
4 MB
```

---

# 58. Phase 4：Memory Pool 与 Message Lifecycle

## 目标

去除每条消息：

```text
dynamic mmap
malloc
```

改成预分配：

```text
Memory Pool
```

实现：

```text
Chunk
Chunk Header
Chunk Handle
Free List
Reference Count
```

实现状态：

```text
FREE
LOANED
PUBLISHED
RELEASED
```

支持：

```text
1 Publisher
N Subscribers
```

一个 Payload 只能保存一次。

## 验收

启动：

```text
1 Publisher
4 Subscribers
```

验证：

```text
payload 地址来自同一个 SHM Chunk

ref_count 正确

所有 Subscriber release 后 chunk 可再次分配
```

---

# 59. Phase 5：Ring Buffer / Backpressure / Loaned Sample

## 目标

实现：

```text
Subscriber Queue
Ring Buffer
```

实现：

```text
DROP_NEWEST
DROP_OLDEST
BLOCK_WITH_TIMEOUT
```

然后实现：

```cpp
publisher.loan()
```

与：

```text
LoanedSample
SampleView
```

如果时间允许升级：

```text
eventfd
```

通知。

## 单元测试

重点：

```text
ring wrap
queue full
queue empty
drop oldest
drop newest
block timeout
loan without publish
loan then publish
double publish protection
SampleView release
```

---

# 60. Phase 6：鲁棒性与生命周期

## 目标

实现：

```text
Heartbeat
Timeout
Dead Process Detection
Resource Cleanup
```

Fault Injection：

```bash
kill -9 publisher_pid

kill -9 subscriber_pid
```

验证：

```text
Registry detects
Endpoint removed
new endpoint can reconnect
system keeps running
```

测试：

```text
Queue Exhaustion
Pool Exhaustion
Invalid Message Size
Topic Type Mismatch
Duplicate Node
Duplicate Publisher
```

---

# 61. Phase 7：ROS2 Adapter

## 目标

完成：

```text
Middleware
↕
ROS2
```

第一版类型：

```text
String
Twist
Image
```

实现：

```text
ros2_to_mw_bridge
mw_to_ros2_bridge
```

提供：

```text
launch
config
README
```

## 验收

ROS2：

```bash
ros2 topic pub ...
```

能够：

```text
ROS2
 ↓
Middleware
 ↓
Custom Subscriber
```

反方向亦可运行。

Image 使用大消息测试。

---

# 62. Phase 8：Benchmark

## 目标

建立自动实验系统。

运行：

```text
UDS
SHM Copy
SHM Loan
ROS2 Baseline
```

每种测试：

```text
64 B
1 KB
4 KB
64 KB
1 MB
4 MB
```

Topology：

```text
1→1
1→2
1→4
```

输出：

```text
CSV
JSON
PNG
```

至少绘制：

```text
Latency vs Message Size

Throughput vs Message Size

CPU vs Message Size

Subscriber Count vs Throughput
```

---

# 63. Phase 8.1：Profiling

Benchmark 完成后再使用：

```text
perf
strace
```

分析：

```text
syscalls
CPU hotspot
context switch
allocation
copy
```

然后只针对明确瓶颈优化。

禁止：

> 先假设问题，再为了简历硬加 Lock-Free。

---

# 64. Phase 9：Documentation / Demo / Resume

最终整理：

```text
README
Architecture
Protocol
Memory Model
Benchmark
Failure Model
ROS2 Adapter
Known Limitations
```

制作：

```text
Architecture Diagram
Demo GIF
Benchmark Chart
Terminal Demo
```

准备最终简历表达。

---

# 65. 必须完成的功能

- [ ] C++17 Middleware Core
- [ ] Publisher API
- [ ] Subscriber API
- [ ] Unix Domain Socket baseline
- [ ] Registry Daemon
- [ ] Topic Discovery
- [ ] Node Registration
- [ ] mwctl
- [ ] Shared Memory Transport
- [ ] Shared Memory RAII
- [ ] Memory Pool
- [ ] Chunk Lifecycle
- [ ] Multi Subscriber
- [ ] Reference Counting
- [ ] Subscriber Queue
- [ ] Backpressure
- [ ] Loaned Sample
- [ ] Runtime Metrics
- [ ] Heartbeat
- [ ] Crash Detection
- [ ] Resource Cleanup
- [ ] ROS2 Adapter
- [ ] Automated Benchmark
- [ ] ROS2 Baseline
- [ ] Unit Test
- [ ] Integration Test
- [ ] README
- [ ] Architecture Documentation
- [ ] Demo

---

# 66. 高级功能

有余力再做：

- [ ] SPSC Lock-Free Ring Buffer
- [ ] eventfd Notification
- [ ] epoll WaitSet
- [ ] SCM_RIGHTS fd passing
- [ ] memfd_create
- [ ] Outstanding Loan Recovery
- [ ] Per-thread Memory Cache
- [ ] CPU Affinity
- [ ] Scheduler Experiment
- [ ] perf profiling automation
- [ ] PointCloud2 ROS Bridge
- [ ] Multi Publisher Topic
- [ ] TCP Remote Transport
- [ ] Docker
- [ ] CI
- [ ] ASan / UBSan / TSan Pipeline

---

# 67. 第一版明确不做

为了防止项目失控，第一版禁止加入：

```text
完整 DDS
完整 RTPS
自研 ROS2 RMW
DDS Discovery Protocol
Distributed Discovery
Cross-machine Auto Discovery
Persistent Message
Durability
Exactly Once
Reliable Retransmission Protocol
Security
Authentication
Encryption
IDL Compiler
Schema Code Generator
RDMA
DPDK
GPU DMA
Hard Real-Time Guarantee
完整 ROS2 QoS
```

这些内容可以学习，但不是秋招项目第一版的实现目标。

---

# 68. Multi-Publisher 限制

第一版建议：

> 一个 Topic 只允许一个 Active Publisher。

但允许：

```text
1 Publisher
N Subscribers
```

原因：

> 这样能够把核心问题集中在共享内存、多消费者、生命周期和性能，而不提前进入 MPSC 顺序与竞争问题。

Multi-Publisher 作为高级功能。

必须在 README Known Limitations 中明确说明。

---

# 69. 工程质量要求

必须具备：

```text
C++17
CMake
RAII
Smart Pointer
Move Semantics
Atomic
std::chrono
gtest
clang-format
Git
Linux
```

编译建议开启：

```text
-Wall
-Wextra
-Wpedantic
```

Debug Pipeline：

```text
ASan
UBSan
```

并发模块后期使用：

```text
TSan
```

---

# 70. C++ 代码原则

禁止：

```text
全局裸指针
手工散落 close()
手工散落 munmap()
不受控 new/delete
巨大 God Class
所有代码放 main.cpp
```

优先：

```text
RAII
Move-only Resource
Clear Ownership
Small Interface
Explicit Error
Bounded Resource
```

---

# 71. 错误处理

例如：

```cpp
enum class ErrorCode {
    Ok,
    RegistryUnavailable,
    TopicNotFound,
    TypeMismatch,
    QueueFull,
    MemoryPoolExhausted,
    MessageTooLarge,
    Timeout,
    ConnectionLost
};
```

业务 API 不应大量依赖：

```text
print error
return -1
```

应形成统一 Error Model。

---

# 72. Unit Test

重点覆盖：

### IPC

```text
socket frame
shared memory map
invalid segment
```

### Memory

```text
pool allocation
pool release
double release protection
size class selection
pool exhaustion
```

### Queue

```text
full
empty
wrap around
overflow policy
```

### Lifecycle

```text
loan
publish
cancel
take
release
ref count
```

### Registry

```text
register
duplicate
subscribe
unsubscribe
timeout
```

---

# 73. Integration Test

至少：

```text
registry
+
publisher
+
subscriber
```

自动启动。

验证：

```text
payload
sequence
message count
process exit
```

Fault Integration：

```text
kill publisher
kill subscriber
slow subscriber
memory exhaustion
```

---

# 74. Sanitizer Test

建议增加：

```bash
cmake ... -DENABLE_ASAN=ON

cmake ... -DENABLE_UBSAN=ON
```

后期单独：

```text
TSan build
```

尤其检查：

```text
Ring Buffer
Reference Count
Shared State
Heartbeat
```

---

# 75. Benchmark 公平性要求

所有实现使用：

```text
same host
same message size
same duration
same topology
same payload
```

每次输出：

```text
machine info
CPU
kernel
compiler
build type
ROS2 version
RMW implementation
test config
```

Benchmark 必须：

```text
Release build
```

不能用 Debug 数据写进 README 性能结论。

---

# 76. README 最终内容

必须包含：

1. Project Overview；
2. Why This Project；
3. Architecture；
4. Control Plane；
5. Data Plane；
6. Publisher / Subscriber API；
7. Shared Memory Layout；
8. Message Lifecycle；
9. Memory Pool；
10. Backpressure；
11. Failure Model；
12. Build；
13. Quick Start；
14. mwctl；
15. ROS2 Adapter；
16. Benchmark Method；
17. Benchmark Results；
18. Performance Analysis；
19. Known Limitations；
20. Future Work。

---

# 77. 最终 Demo 1：Basic Pub/Sub

```bash
./mw_registryd
```

然后：

```bash
./mw_subscriber \
    --topic /demo
```

再：

```bash
./mw_publisher \
    --topic /demo \
    --rate 100
```

显示：

```text
sequence
payload size
latency
```

---

# 78. Demo 2：Large Message

发送：

```text
4 MB
```

消息：

```bash
./mw_benchmark_pub \
    --size 4M \
    --transport shm
```

Subscriber 展示：

```text
throughput
latency
drop
```

---

# 79. Demo 3：Multi Subscriber

```text
Publisher
  │
  ├→ Subscriber 1
  ├→ Subscriber 2
  ├→ Subscriber 3
  └→ Subscriber 4
```

展示：

> Payload 只在共享内存保存一份。

---

# 80. Demo 4：Backpressure

Subscriber 人为：

```text
sleep 100ms
```

观察：

```text
queue depth
drop count
overflow policy
```

切换：

```text
DROP_NEWEST
DROP_OLDEST
BLOCK
```

---

# 81. Demo 5：Crash Recovery

运行：

```bash
kill -9 <subscriber_pid>
```

观察：

```text
mwctl node list
```

Subscriber 从：

```text
ALIVE
```

到：

```text
DEAD
```

并完成资源清理。

---

# 82. Demo 6：ROS2 Adapter

```text
Custom Publisher
      ↓
Middleware
      ↓
ROS2 Adapter
      ↓
ROS2 Image Topic
```

然后：

```bash
ros2 topic hz ...
```

验证 ROS2 侧正常接收。

---

# 83. Demo 7：Benchmark Dashboard

最终展示：

```text
Latency p50/p99
Throughput
CPU
Memory
```

例如图：

```text
UDS
vs
SHM Copy
vs
SHM Loan
vs
ROS2 Baseline
```

---

# 84. 技术风险

## 风险 1：Shared Memory 生命周期复杂

降级策略：

```text
先完成固定大小 Segment
```

再做：

```text
Memory Pool
```

---

## 风险 2：引用计数泄漏

必须：

```text
Unit Test
+
Crash Test
+
Metrics
```

---

## 风险 3：Lock-Free Bug

第一版：

```text
Mutex Version
```

必须先工作。

之后再：

```text
SPSC Atomic Version
```

---

## 风险 4：Benchmark 不公平

必须保存：

```text
Benchmark Config
Environment
Raw Data
```

不能只展示对自己有利的数据。

---

## 风险 5：Zero-Copy 名不副实

README 必须分别说明：

```text
UDS
SHM Copy
SHM Loan
ROS Bridge
```

各自 Copy Path。

---

# 85. 推荐技术栈

```text
Language:
C++17

OS:
Ubuntu Linux

Build:
CMake

Test:
GoogleTest

IPC:
Unix Domain Socket
POSIX Shared Memory

Linux APIs:
mmap
shm_open
epoll
eventfd
optional SCM_RIGHTS

Concurrency:
std::mutex
std::atomic
condition_variable

ROS:
ROS2 Jazzy

Evaluation:
Python

Plot:
matplotlib

Profiling:
perf
strace

Debug:
gdb
ASan
UBSan
TSan

Version Control:
Git
GitHub
```

---

# 86. 面试中必须能回答：Linux IPC

必须可以回答：

```text
进程和线程有什么区别？

为什么不同进程不能直接访问普通 heap？

mmap 做了什么？

Shared Memory 为什么快？

shm_open 和普通文件 mmap 有什么区别？

Unix Domain Socket 和 TCP Socket 有什么区别？

为什么还需要 Control Plane？

为什么有 Shared Memory 还需要 Notification？
```

---

# 87. 面试中必须能回答：Memory

```text
为什么使用 Memory Pool？

频繁 malloc 有什么问题？

Internal Fragmentation 是什么？

为什么需要 Size Class？

Chunk 怎么分配？

Chunk 怎么回收？

为什么使用 ref_count？

Subscriber Crash 怎么处理？

什么情况下会发生 Memory Leak？
```

---

# 88. 面试中必须能回答：Concurrency

```text
mutex 有什么成本？

atomic 和 mutex 有什么区别？

什么是 CAS？

什么是 Memory Ordering？

什么是 SPSC？

什么是 MPSC？

为什么 Ring Buffer 适合通信 Queue？

什么是 False Sharing？

为什么 Cache Line 会影响性能？

ABA Problem 是什么？
```

不要求项目全部实现这些算法，但必须知道相关概念。

---

# 89. 面试中必须能回答：Middleware

```text
什么是 Pub/Sub？

为什么需要 Discovery？

Topic 和 Endpoint 如何建模？

Control Plane 和 Data Plane 有什么区别？

为什么第一版限制一个 Publisher？

Backpressure 是什么？

Slow Consumer 如何处理？

DROP_OLDEST 和 DROP_NEWEST 有什么差异？

为什么机器人传感器场景经常关心最新数据？

为什么不能简单无限扩大 Queue？
```

---

# 90. 面试中必须能回答：Zero Copy

```text
什么叫 Zero-Copy？

Shared Memory 就一定 Zero-Copy 吗？

你的 SHM Copy 路径有几次 Payload Copy？

Loaned Sample 为什么可以减少 Copy？

Publisher Loan 后不 Publish 怎么办？

多 Subscriber 如何共享同一个 Payload？

为什么 ROS2 Adapter 不一定还是 Zero-Copy？
```

---

# 91. 面试中必须能回答：Performance

```text
Latency p50 和 p99 有什么区别？

为什么平均延迟不够？

为什么大消息更适合 Shared Memory？

为什么小消息下 Socket 可能并不差？

Throughput 和 Latency 是否可能冲突？

CPU 使用率怎么测？

System Call 为什么有成本？

Context Switch 是什么？

怎么判断真正的性能瓶颈？
```

---

# 92. 面试中必须能回答：ROS2

```text
ROS2 Node 如何通信？

Topic 是什么？

DDS 在 ROS2 中承担什么角色？

RMW 是什么？

为什么 ROS2 可以更换 Middleware？

QoS 是什么？

ROS2 intra-process 是什么？

Loaned Message 是什么？

为什么本项目不直接实现完整 RMW？

ROS2 Adapter 与 RMW Implementation 有什么区别？
```

---

# 93. 项目完成后简历定位

项目名称建议：

> **C++ 高性能发布订阅通信中间件与 ROS2 适配框架**

或者：

> **Linux C++ 高性能共享内存通信中间件**

简历重点不能写成：

> 使用 Shared Memory 完成进程通信。

而应该体现：

> **自主设计并实现本机多进程发布订阅通信中间件，构建控制面与共享内存数据面，实现内存池、消息生命周期、多订阅者共享、背压及故障处理，并通过系统 Benchmark 量化不同 IPC 路径在大消息场景下的延迟与吞吐表现。**

---

# 94. 完成后可体现的 C++ 能力

```text
Modern C++
RAII
Move Semantics
Atomic
Resource Ownership
Library Design
Error Handling
Testing
CMake Packaging
```

---

# 95. 完成后可体现的 Linux 能力

```text
Process
IPC
Unix Domain Socket
Shared Memory
mmap
epoll
eventfd
fd lifecycle
process liveness
```

---

# 96. 完成后可体现的系统能力

```text
Control Plane
Data Plane
Registry
Discovery
Pub/Sub
Backpressure
Resource Lifecycle
Fault Recovery
Observability
```

---

# 97. 完成后可体现的性能工程能力

```text
Memory Pool
Reduced Copy
Loaned Sample
Ring Buffer
Latency
p99
Throughput
CPU
Profiling
Benchmark
```

---

# 98. 完成后可体现的机器人能力

```text
ROS2 Communication
Middleware
Large Sensor Message
Robot Software Infrastructure
ROS2 Adapter
```

---

# 99. 简历项目最终可能包含的内容

只有对应功能真实完成后才能写。

示例：

> 基于 Linux + C++17 自主实现本机多进程 Pub/Sub 通信中间件，将 Unix Domain Socket 控制面与 Shared Memory 数据面解耦，支持 Topic 注册发现、Publisher/Subscriber 生命周期管理和运行状态查询。

> 设计共享内存 Memory Pool 与 Chunk 生命周期管理机制，使用引用计数实现多 Subscriber 共享同一 Payload，并实现 Queue Depth、DROP_NEWEST / DROP_OLDEST / BLOCK 等背压策略。

> 实现 Loaned Sample API，使 Publisher 可直接写入共享内存缓冲区，并基于 RAII 管理消息申请、发布、消费与回收流程。

> 构建 ROS2 Adapter 与自动 Benchmark，对 UDS、SHM Copy、SHM Loan 和 ROS2 Baseline 在不同消息大小和 Subscriber 数量下的 p50/p99 延迟、吞吐量、CPU 和丢包率进行量化比较。

具体数字必须等项目完成后填写。

---

# 100. Git 仓库开发原则

仓库：

```text
main
```

必须始终保持：

```text
buildable
testable
```

每个 Phase 推荐：

```text
feat/phase-1-uds
feat/phase-2-registry
...
```

完成后合入 main。

可以打 Tag：

```text
phase-0
phase-1
phase-2
...
phase-9
```

---

# 101. Commit 原则

不要：

```text
一个 Commit 生成 50 个文件
```

建议按：

```text
core
test
docs
```

拆分。

例如：

```text
feat: add unix socket transport

test: add socket frame tests

docs: document uds transport
```

保证 Git History 可以反映开发过程。

---

# 102. 后续给 GPT 的任务

这份规划书完成后，不直接把整个项目一次性交给 Codex。

下一步应让 GPT 根据：

```text
PROJECT_PLAN.md
```

输出：

1. Phase 0～9 更详细任务拆解；
2. 每个 Phase 独立 Codex Prompt；
3. 当前 Phase 需要创建的文件；
4. API 约束；
5. 数据结构；
6. 测试；
7. 验收脚本；
8. 禁止提前实现的内容。

---

# 103. 后续给 Codex 的实现原则

每个 Phase 开始前必须要求 Codex：

1. 阅读 `PROJECT_PLAN.md`；
2. 阅读当前仓库；
3. 总结当前已实现状态；
4. 只处理当前 Phase；
5. 先输出设计；
6. 列出新增文件；
7. 列出修改文件；
8. 说明每个文件职责；
9. 说明核心数据结构；
10. 说明资源 Ownership；
11. 说明线程 / 进程模型；
12. 说明错误处理；
13. 再开始编码；
14. 编译；
15. 运行 Unit Test；
16. 运行 Integration Test；
17. 修复所有错误；
18. 给出运行命令；
19. 给出验收结果；
20. 更新当前阶段文档；
21. 生成 `PHASE_X_REPORT.md`；
22. 不实现下一 Phase。

---

# 104. Codex 不允许自由扩展范围

如果 Codex 判断：

```text
应该加入 protobuf
应该加入 Boost
应该加入 ZeroMQ
应该加入 iceoryx
应该加入 DDS
```

不得直接加入。

必须：

```text
说明原因
说明收益
说明成本
说明是否违反 PROJECT_PLAN
```

然后由项目负责人决定。

---

# 105. Codex 第三方依赖原则

核心 Middleware 第一版尽量减少第三方依赖。

优先：

```text
C++ STL
Linux API
GoogleTest
```

不要为了方便直接依赖：

```text
ZeroMQ
iceoryx
Fast DDS Core
Boost IPC
```

否则会削弱：

> 自主实现 Middleware Core

这一项目价值。

ROS2 Adapter 除外。

---

# 106. AI 开发后的最低人工检查

虽然主要使用 GPT + Codex 完成代码，实现阶段至少需要人工确认：

```text
项目能编译
测试能运行
Demo 能运行
数据结构与你的规划一致
Benchmark 没有明显错误
```

深度源码学习可以等完整实现后进行。

但不建议出现：

> Phase 0 写完一直不运行，直接让 AI 连续生成到 Phase 9。

每个阶段至少完成：

```text
Build
Test
Acceptance
Git Commit
```

再进入下一阶段。

---

# 107. 完整实现后的学习顺序

项目全部完成以后，不建议从：

```text
第一行代码
```

一路读到最后。

推荐：

```text
Level 1
系统结构
        ↓
Level 2
进程 / Topic / 数据流
        ↓
Level 3
Public API
        ↓
Level 4
Control Plane
        ↓
Level 5
Unix Domain Socket
        ↓
Level 6
Shared Memory
        ↓
Level 7
Memory Pool
        ↓
Level 8
Chunk Lifecycle
        ↓
Level 9
Ring Buffer / Backpressure
        ↓
Level 10
Loaned Sample
        ↓
Level 11
Crash Recovery
        ↓
Level 12
ROS2 Adapter
        ↓
Level 13
Benchmark / Profiling
```

---

# 108. 学习第一阶段：只看系统行为

运行：

```text
Registry
Publisher
Subscriber
mwctl
```

弄懂：

```text
有哪些进程？

谁注册？

谁发现谁？

数据走哪里？

控制消息走哪里？
```

暂时不逐行读 C++。

---

# 109. 学习第二阶段：IPC

重点学习：

```text
socket
UDS
mmap
shared memory
fd
process address space
```

然后回头读：

```text
unix_socket.cpp
shared_memory.cpp
```

---

# 110. 学习第三阶段：C++ Resource Management

重点：

```text
RAII
move constructor
move assignment
destructor
ownership
```

研究：

```text
UniqueFd
SharedMemoryRegion
LoanedSample
SampleView
```

---

# 111. 学习第四阶段：并发

学习：

```text
mutex
atomic
memory ordering
ring buffer
producer consumer
```

然后读：

```text
ring_buffer.cpp
memory_pool.cpp
```

---

# 112. 学习第五阶段：系统设计

学习：

```text
Registry
Discovery
Control Plane
Data Plane
Backpressure
Failure Model
```

此时重点不是语法，而是：

> 为什么系统要这样设计。

---

# 113. 学习第六阶段：性能

使用：

```text
perf
strace
benchmark result
```

回答：

```text
时间花在哪里？

Copy 在哪里？

Syscall 在哪里？

为什么 4 MB 与 64 B 的结果不同？

为什么 p99 会抖？
```

---

# 114. 学习第七阶段：ROS2

最后再将自研 Middleware 与：

```text
ROS2
RMW
DDS
Loaned Message
Intra-process
```

进行对应。

这时候学习 ROS2 Middleware 会比单纯看文档容易很多。

---

# 115. 项目完成判断标准

项目是否成功，不以：

```text
代码量
文件数量
功能数量
```

判断。

而以以下问题判断：

> **是否真正理解 Publisher 到 Subscriber 的数据路径？**

> **是否能解释一次消息传递发生了几次 Copy？**

> **是否能解释 Shared Memory 中 Payload 的生命周期？**

> **是否能解释多个 Subscriber 时资源如何回收？**

> **是否能解释 Subscriber 过慢怎么办？**

> **是否能解释进程崩溃后资源怎么办？**

> **是否能通过 Benchmark 证明设计在哪些情况下有效？**

> **是否可以解释为什么项目不实现完整 DDS / RMW？**

如果这些问题都可以清楚回答，即使代码量没有特别大，也已经具备较高的简历和面试价值。

---

# 116. 一句话项目定义

> **基于 Linux 与 C++17 自主设计本机多进程发布订阅通信中间件，构建 Unix Domain Socket 控制面与 Shared Memory 数据面，实现 Topic 注册发现、Memory Pool、Chunk 生命周期、多订阅者共享、背压及进程故障处理，并通过 ROS2 Adapter 接入机器人软件生态，基于系统化 Benchmark 对 Socket、Shared Memory 与 ROS2 通信链路的延迟、吞吐和资源开销进行定量分析。**

---

# 117. 项目核心故事

项目最终在面试中应该形成一个非常清晰的故事：

```text
我之前会使用 ROS2
        ↓
我想知道 ROS2 通信下面到底发生了什么
        ↓
所以我自己实现了一个简化 Pub/Sub Middleware
        ↓
先实现 Socket Baseline
        ↓
然后实现 Shared Memory Data Plane
        ↓
解决 Memory Pool / Queue / Backpressure
        ↓
解决 Multi Subscriber / Message Lifetime
        ↓
解决 Process Crash
        ↓
再通过 ROS2 Adapter 接回 ROS2
        ↓
最后做系统 Benchmark
```

这个故事比：

> “我为了简历写了一个共享内存项目。”

更完整。

---

# 118. 项目最终能力闭环

```text
                     C++
                      │
            Resource Management
                      │
                  Concurrency
                      │
                     Linux
                      │
          Socket / Shared Memory
                      │
                    IPC
                      │
                   Pub/Sub
                      │
                 Middleware
                      │
                   ROS2
                      │
                Robot Software
```

同时结合已有项目：

```text
AMR
 ↓
Robot Application

Middleware
 ↓
Robot Infrastructure

Linux WeakNet
 ↓
Linux System

Raft
 ↓
Distributed System
```

最终 C++ 项目组合体现：

> **从 Linux / 网络 / 分布式，到机器人应用，再到底层机器人基础软件的一套完整 C++ 系统能力。**

---

# 119. 下一步

下一步不要直接完整编码。

应先把本规划书保存为：

```text
PROJECT_PLAN.md
```

然后将规划书交给 GPT，要求：

> **严格基于 PROJECT_PLAN.md，将 Phase 0～9 进一步拆解成可以逐阶段交给 Codex 的开发任务。不得改变总架构和项目边界；每个 Phase 必须给出目标、前置条件、文件变化、核心接口、数据结构、测试用例、运行命令、验收标准、禁止事项和对应 Codex Prompt。**

之后：

```text
GPT
 ↓
Phase 0 Prompt
 ↓
Codex
 ↓
Build / Test / Acceptance
 ↓
Git Commit
 ↓
Phase 1
 ↓
...
 ↓
Phase 9
```

最终全部实现后，再基于完整仓库进行系统学习、源码分析和面试准备。

---

# 120. 总原则

本项目不是为了：

```text
功能越多越好
代码越复杂越好
Lock-Free 越多越好
比 Fast DDS 更快
```

而是为了：

> **通过亲自构建一个边界清晰的小型通信中间件，把 C++、Linux、IPC、共享内存、并发、资源管理、性能工程和 ROS2 机器人基础软件真正串起来。**

这应该作为整个项目开发过程中最高优先级的判断标准。