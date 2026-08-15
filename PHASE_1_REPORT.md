# Phase 1 Report

## Scope Implemented

Phase 1 implements a Linux-local Unix Domain Socket Pub/Sub baseline for one publisher, one
subscriber, and one explicitly configured topic. Payload data is copied through a stream socket.
The phase adds the public Context/Publisher/Subscriber API, bounded message configuration, explicit
frame encoding, sequence and monotonic timestamp metadata, blocking receive with timeout, basic
disconnect/reconnect behavior, deterministic demo programs, and focused unit/integration tests.

## Files Added

- `CODEX_TASKS/PHASE_1.md`
- `docs/UDS_BASELINE.md`
- `examples/CMakeLists.txt`
- `examples/ping_common.hpp`
- `examples/ping_publisher/main.cpp`
- `examples/ping_subscriber/main.cpp`
- `middleware/include/mw/config.hpp`
- `middleware/include/mw/context.hpp`
- `middleware/include/mw/message.hpp`
- `middleware/include/mw/publisher.hpp`
- `middleware/include/mw/result.hpp`
- `middleware/include/mw/subscriber.hpp`
- `middleware/src/context.cpp`
- `middleware/src/publisher.cpp`
- `middleware/src/subscriber.cpp`
- `middleware/src/detail/frame_protocol.cpp`
- `middleware/src/detail/frame_protocol.hpp`
- `middleware/src/detail/socket_io.cpp`
- `middleware/src/detail/socket_io.hpp`
- `middleware/src/detail/unique_fd.hpp`
- `middleware/src/detail/unix_socket.cpp`
- `middleware/src/detail/unix_socket.hpp`
- `tests/unit/frame_protocol_test.cpp`
- `tests/unit/socket_io_test.cpp`
- `tests/integration/pubsub_test.cpp`
- `tests/integration/process_pubsub_test.cpp`
- `PHASE_1_REPORT.md`

## Files Modified

- `CMakeLists.txt`
- `README.md`
- `examples/external_consumer/main.cpp`
- `middleware/CMakeLists.txt`
- `tests/CMakeLists.txt`

## Public API

- `Context` owns a validated node name and creates explicitly configured endpoints.
- `PublisherConfig` and `SubscriberConfig` contain only `socket_path` and `max_message_size`.
- `Publisher` is move-only. `publish()` returns `PublishResult` with `ErrorCode`, sequence, and
  accepted payload size.
- `Subscriber` is move-only. `take()` is nonblocking and `waitAndTake()` accepts a millisecond
  timeout. Both return an owning `ReceivedMessage`; `lastError()` distinguishes timeout,
  disconnect, invalid frame, oversize, and I/O failure.
- `ReceivedMessage` owns its payload vector plus sequence and publish timestamp.

Endpoint construction rejects empty names/paths, invalid topic names, and protocol-incompatible
size limits. Socket setup failures are reported as `std::system_error` with the failing operation
and `errno` details.

## Frame Protocol

Each stream frame contains a 24-byte header and `payload_size` bytes:

```text
uint32 magic             0x4D573031 (MW01)
uint32 payload_size
uint64 sequence
uint64 publish_timestamp_ns
payload[payload_size]
```

All integers are encoded explicitly in big-endian byte order; C++ structure padding is not sent.
The subscriber validates magic and maximum size before allocating payload storage. Empty payloads
are valid. Each publisher starts at sequence 1, and timestamps come from
`std::chrono::steady_clock` in nanoseconds.

## Connection Model

The subscriber creates, binds, listens, polls, and accepts. The publisher creates and connects. The
subscriber retains one listening socket while an accepted connection comes and goes. EOF or a
protocol failure discards only the accepted connection, so a new publisher can connect to the same
subscriber.

## FD / Socket Ownership

- Internal `UniqueFd` is non-copyable, movable, and closes its descriptor in its destructor.
- A publisher owns one connected descriptor.
- A subscriber owns the `UnixListener` and at most one accepted descriptor.
- `UnixListener` owns the pathname only after successful bind. Clean destruction closes the
  listening descriptor and unlinks the path.
- Startup does not unlink an existing path. Stale-path recovery after a crash is outside Phase 1.

## Thread Model

No middleware worker thread or thread pool exists. `publish()` performs `send()` loops on the
application thread. `take()`/`waitAndTake()` perform `poll()`, `accept4()`, and incremental `recv()`
on the application thread. Test-only threads allow a blocking 1 MB send and receive to progress in
one test process; another integration test uses `fork()` to verify a genuine process boundary.

## Error Handling

- Publisher `writeAll()` retries `EINTR`, handles partial writes, and uses `MSG_NOSIGNAL`.
- Subscriber accepted sockets are nonblocking. Partial header and payload state is retained across
  API calls and completed through `poll()` under one deadline.
- A bad magic closes the connection with `InvalidFrame`.
- An oversized frame closes the connection with `MessageTooLarge` before payload allocation.
- EOF after any partial frame is `InvalidFrame`; clean EOF between frames is `ConnectionLost`.
- Publisher peer closure returns `ConnectionLost`/`IoError` instead of raising `SIGPIPE`.
- Bind, listen, connect, and socket-creation failures throw `std::system_error`; invalid API
  configuration throws `std::invalid_argument`.

## Build Commands

The command runner rejected the requested `rm -rf -- build _install build_external ...` form. Only
the known `build_preflight` directory existed at that point, so it was removed with:

```bash
cmake -E remove_directory build_preflight
```

The clean acceptance build used:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
```

Result: PASS with GCC 13.3.0 and no compiler warnings.

## Unit Tests

Command:

```bash
ctest --test-dir build --output-on-failure
```

Result: PASS, 16/16 total tests. Unit coverage includes version linkage; header field
encode/decode, size rejection, magic and payload-bound validation; forced partial reads/writes;
synthetic `EINTR`; transferred-byte reporting; peer disconnect; and `SIGPIPE` suppression.

## Integration Tests

Integration coverage uses real UDS stream sockets and verifies:

- 0 B, 64 B, 1 KB, and 1 MB payload equality;
- sequence and monotonic timestamp metadata;
- explicit wait timeout;
- 256 strictly increasing messages without gaps or duplicates;
- partial-header preservation across `take()` calls;
- bad magic, oversized payload declaration, truncated header, and truncated payload;
- publisher peer closure without process termination;
- subscriber disconnect detection and acceptance of a new publisher;
- 64 deterministic 128-byte messages across a real `fork()` process boundary.

All integration tests passed. The cross-process child returns through subscriber scope before
`_exit()`, so clean pathname ownership is exercised and no socket artifact remains.

## Install / Export Verification

Commands:

```bash
cmake --install build --prefix /home/chaos/projects/cpp_robot_middleware/_install
cmake -S examples/external_consumer -B build_external \
  -DCMAKE_PREFIX_PATH=/home/chaos/projects/cpp_robot_middleware/_install
cmake --build build_external -j
./build_external/mw_external_consumer
```

Result: PASS. All Phase 1 public headers were installed, the standalone project found and linked
`mw::mw_core`, and execution printed `0.1.0`.

## 100000 Message Test

Commands used the Debug-build demo executables with `/tmp/mw_phase1_100k.sock`, count 100000, and
payload size 64.

Publisher result:

```text
sent=100000 publish_errors=0
exit status=0
```

Subscriber result:

```text
received=100000 sequence_errors=0 payload_errors=0
exit status=0
```

Result: PASS.

## Large Message Test

Standalone cross-process demo results:

| Payload | Count | Sent | Received | Sequence errors | Payload errors | Exit status |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 0 B | 1000 | 1000 | 1000 | 0 | 0 | publisher 0, subscriber 0 |
| 1 KB | 1000 | 1000 | 1000 | 0 | 0 | publisher 0, subscriber 0 |
| 1 MB | 10 | 10 | 10 | 0 | 0 | publisher 0, subscriber 0 |

The 64 B result is the 100000-message acceptance above. Result: PASS for all required payload sizes.

## Sanitizer Result

Commands:

```bash
cmake -S . -B build_sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_ASAN=ON \
  -DENABLE_UBSAN=ON
cmake --build build_sanitize -j
ctest --test-dir build_sanitize --output-on-failure
```

Result: PASS, 16/16 tests with combined AddressSanitizer and UndefinedBehaviorSanitizer. No
sanitizer diagnostics were reported.

## Formatting And Final Checks

All C++ files were formatted with clang-format 18.1.3. `clang-format --dry-run --Werror` and
`git diff --check` passed. Generated build/install directories, binaries, compile commands, logs,
and socket files are ignored or removed before delivery; no Phase 2 source exists.

## Known Limitations

- Only one publisher, one subscriber, and one explicitly configured topic are supported.
- No registry or discovery.
- No shared memory or memory pool.
- No subscriber queue or backpressure policy.
- No loaned sample or zero-copy data path.
- No heartbeat, crash recovery, or automatic stale-path reclamation.
- No ROS2 adapter.
- No benchmark framework or performance claim.

## Phase Boundary Confirmation

Phase 2 was not implemented.
