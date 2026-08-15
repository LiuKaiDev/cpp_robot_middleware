# Phase 2 Report

## Scope

Phase 2 implements a Linux-local Registry / Discovery / `mwctl` control plane while preserving the
Phase 1 direct UDS mode and copied-payload frame transport. It adds node/topic/endpoint identity,
exact type matching, one-active-publisher enforcement, clean lifecycle requests, arbitrary
publisher/subscriber startup order, live registry inspection, and malformed-control-frame safety.

## Files Added

- `CODEX_TASKS/PHASE_2.md`
- `docs/CONTROL_PLANE.md`
- `middleware/src/detail/control_protocol.cpp`
- `middleware/src/detail/control_protocol.hpp`
- `middleware/src/detail/registry_client.cpp`
- `middleware/src/detail/registry_client.hpp`
- `registry/CMakeLists.txt`
- `registry/include/mw/registry/registry_server.hpp`
- `registry/include/mw/registry/registry_state.hpp`
- `registry/src/main.cpp`
- `registry/src/registry_server.cpp`
- `registry/src/registry_state.cpp`
- `tools/mwctl/CMakeLists.txt`
- `tools/mwctl/main.cpp`
- `tests/unit/control_protocol_test.cpp`
- `tests/unit/registry_state_test.cpp`
- `tests/integration/registry_discovery_test.cpp`
- `PHASE_2_REPORT.md`

## Files Modified

- `CMakeLists.txt`
- `README.md`
- `examples/ping_common.hpp`
- `examples/ping_publisher/main.cpp`
- `examples/ping_subscriber/main.cpp`
- `middleware/CMakeLists.txt`
- `middleware/include/mw/config.hpp`
- `middleware/include/mw/context.hpp`
- `middleware/include/mw/publisher.hpp`
- `middleware/include/mw/result.hpp`
- `middleware/include/mw/subscriber.hpp`
- `middleware/src/context.cpp`
- `middleware/src/detail/unix_socket.cpp`
- `middleware/src/detail/unix_socket.hpp`
- `middleware/src/publisher.cpp`
- `middleware/src/subscriber.cpp`
- `tests/CMakeLists.txt`

## Architecture

Control plane and data plane are separate:

```text
Context / endpoints / mwctl -> RegistryClient -> mw_registryd control UDS
Publisher                  -> Phase 1 frame + payload UDS -> Subscriber
```

`RegistryState` owns pure state rules. `RegistryServer` owns nonblocking control I/O and dispatch.
`RegistryClient` owns correlated synchronous calls. `Context` owns node identity through a shared
registry session but delegates protocol details. The daemon never forwards application payloads.
The original `Context(node_name)` direct configuration remains functional.

## Control Protocol

Each frame is an explicitly encoded 16-byte big-endian `ControlHeader` plus at most 64 KiB of
payload. Header fields are magic `0x4D574332`, version 1, opcode, request ID, and payload size.
Responses repeat the request ID and carry an explicit `ErrorCode`, message, and optional body.

Implemented operations are register/unregister node, advertise/unadvertise topic,
subscribe/unsubscribe topic, resolve endpoint, list nodes, list topics, and query topic. Unit and
daemon-level tests cover round trips, header fields, magic/version/size validation, unknown opcode,
truncated requests, malformed payloads, partial stream framing, and state integrity.

## Node, Topic, And Endpoint Models

- A node has a monotonic `node_id`, unique name, owning control connection, and its endpoint sets.
- A topic has a monotonic `topic_id`, name, type name/hash, current size bound, zero/one publisher,
  and N subscriber records.
- Publisher and subscriber endpoints have monotonic `endpoint_id`, node ID, and topic ID.
- A subscriber endpoint additionally stores its Phase 1 data socket path and maximum message size.
- A topic is removed when clean teardown leaves it with no publisher or subscribers.

Registry list results are name-sorted. A second active publisher returns `DuplicatePublisher`.
Same-topic type-name or type-hash differences return `TypeMismatch`; the registry does not inspect
message fields.

## Registry State And Discovery Flow

Subscriber-first mode records the subscription before the publisher advertises. The publisher then
resolves the stored subscriber path and connects directly.

Publisher-first mode records the advertisement immediately. Its first `publish()` issues a resolve
request that remains pending in the daemon. A later compatible subscription completes the original
request, after which the publisher connects and writes Phase 1 frames without endpoint restart.

## Resource Ownership

- `RegistryServer` owns the control listener, epoll descriptor, accepted control descriptors,
  per-connection partial buffers, pending discovery map, and registry state.
- Internal move-only `UniqueFd` wrappers close every owned descriptor.
- A registry-enabled `Context` creates a `RegistrySession`; endpoints share that owner so node state
  outlives the Context when necessary.
- Publisher/subscriber destructors send unadvertise/unsubscribe. The last session owner sends
  unregister. Cleanup failures during destructors are contained.
- A subscriber still owns its Phase 1 data listener and cleanly unlinks the pathname.

## Process And Thread Model

`mw_registryd` runs one `epoll` event loop and has no worker pool. Publisher, subscriber, and
`mwctl` are separate processes in integration/manual acceptance. Application control requests,
discovery wait, data publish, and data receive execute on caller threads; the middleware creates no
background application thread.

## Error Handling

Configuration errors throw `std::invalid_argument`; registry and protocol failures throw
`MiddlewareError` with explicit codes. The publisher returns transport/discovery errors through
`PublishResult`. The control client validates response opcode, request ID, header, size, and body.
The daemon waits for a complete request before mutating state and responds safely to invalid magic,
unsupported versions, unknown opcodes, oversized declarations, and malformed payloads. Clean
connection loss is isolated to that client.

## Build And CTest

Commands:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Result: PASS with GCC 13.3.0. CTest: PASS, 27/27 tests. The suite contains the unchanged 16 Phase 1
tests plus 11 control protocol, registry state, and registry process-integration tests.

## Sanitizers

Separate Debug builds used `-DENABLE_ASAN=ON` and `-DENABLE_UBSAN=ON`. Each built the complete
daemon/client/examples/test graph and ran full CTest.

- ASan: PASS, 27/27 tests, no diagnostics.
- UBSan: PASS, 27/27 tests, no diagnostics.

Coverage includes control encoding, registry state, client/server process interaction, malformed
control input, both discovery orders, live `mwctl`, and all Phase 1 data-plane tests.

## Manual Publisher-First Result

The daemon started first, followed by a publisher that advertised and blocked in discovery. The
subscriber started afterward without restarting the publisher.

```text
sent=10 publish_errors=0
received=10 sequence_errors=0 payload_errors=0
```

Result: PASS. Connection, payload, sequence, and process exit were correct.

## Manual Subscriber-First Result

The daemon started first, followed by the subscriber and then the publisher.

```text
sent=10 publish_errors=0
received=10 sequence_errors=0 payload_errors=0
```

Result: PASS. Connection, payload, sequence, and process exit were correct.

## Type Mismatch Result

A live registry accepted `/typed` with type `Example` / `hash-a` and rejected a publisher using
`Example` / `hash-b` with `ErrorCode::TypeMismatch`. No data-plane match was made. Result: PASS.

## mwctl Results

The following output came from the built `mwctl` while a real daemon and subscriber were active.

`mwctl node list`:

```text
NODE_ID NODE_NAME
1       ping_subscriber
```

`mwctl topic list`:

```text
TOPIC_ID TOPIC_NAME
1        /ping
```

`mwctl topic info /ping`:

```text
topic_id: 1
topic_name: /ping
type_name: mw.examples.Ping
type_hash: mw.examples.Ping.v1
max_message_size: 4194304
publishers: 0
subscribers: 1
```

All three commands returned exit status 0. The waiting subscriber subsequently matched a publisher
and completed one correct message. Result: PASS.

## Install / Export Verification

`cmake --install` installed the shared library, public headers, `mw_registryd`, `mwctl`, and CMake
package. Both installed tools use a relative `$ORIGIN/../lib` RPATH and ran from the install prefix.
The standalone external consumer configured against that prefix, built, ran, and printed `0.1.0`.
Result: PASS.

## Phase 1 Regression

All 16 pre-existing tests remain enabled and passed. This includes empty, 64 B, 1 KB, and 1 MB
payloads; strict sequences; partial reads/writes and `EINTR`; timeout; malformed/truncated frames;
disconnect/reconnect; peer closure; and real cross-process UDS communication. Direct-mode demos and
the installed external consumer also remain functional. Result: PASS.

## Known Limitations

- Phase 2 is Linux single-host only; there is no distributed discovery.
- No heartbeat, dead-process timeout, crash recovery, or automatic stale state/path cleanup exists.
  A process that exits without lifecycle requests can leave registry state until daemon restart.
- The registry state retains N subscribers, but the current Phase 1 copied-payload path discovers
  one subscriber and does not fan out a payload to all subscribers.
- Registry client calls are synchronous and not multiplexed for concurrent callers. A publisher's
  initial discovery can wait indefinitely for a compatible subscriber.
- The data path copies payload bytes through kernel UDS buffers and is not zero-copy.
- There is no shared memory, memory pool, subscriber queue/backpressure, loaned sample, ROS2
  adapter, or benchmark framework.

## Phase Boundary

Phase 3 was not implemented.
