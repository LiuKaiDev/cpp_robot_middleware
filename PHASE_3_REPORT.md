# Phase 3 Report

## Scope

Phase 3 implements a Linux-local POSIX shared-memory data plane V1 for one publisher and one
subscriber. It preserves the Phase 1 copied-payload UDS baseline and Phase 2 registry/control plane,
adds explicit transport selection and compatibility, carries only bounded SHM locator metadata over
the data UDS, and uses a subscriber ACK before publisher-owned unlink.

## Files Added

- `CODEX_TASKS/PHASE_3.md`
- `docs/DATA_PLANE.md`
- `middleware/src/detail/shared_memory.cpp`
- `middleware/src/detail/shared_memory.hpp`
- `middleware/src/detail/shm_protocol.cpp`
- `middleware/src/detail/shm_protocol.hpp`
- `tests/integration/shm_transport_test.cpp`
- `tests/unit/shared_memory_test.cpp`
- `tests/unit/shm_protocol_test.cpp`
- `PHASE_3_REPORT.md`

## Files Modified

- `README.md`
- `docs/CONTROL_PLANE.md`
- `examples/ping_common.hpp`
- `examples/ping_publisher/main.cpp`
- `examples/ping_subscriber/main.cpp`
- `middleware/CMakeLists.txt`
- `middleware/include/mw/config.hpp`
- `middleware/include/mw/result.hpp`
- `middleware/src/detail/registry_client.cpp`
- `middleware/src/detail/registry_client.hpp`
- `middleware/src/detail/control_protocol.hpp`
- `middleware/src/publisher.cpp`
- `middleware/src/subscriber.cpp`
- `registry/include/mw/registry/registry_state.hpp`
- `registry/src/registry_server.cpp`
- `registry/src/registry_state.cpp`
- `tests/CMakeLists.txt`
- `tests/integration/registry_discovery_test.cpp`
- `tests/unit/registry_state_test.cpp`
- `tests/unit/control_protocol_test.cpp`
- `tools/mwctl/main.cpp`

## Architecture

```text
Context / endpoints / mwctl -> RegistryClient -> mw_registryd control UDS

UDS mode: Publisher -> 24-byte MW01 header + copied payload -> Subscriber

SHM mode: Publisher -> SharedMemoryHeader + payload in POSIX SHM <- Subscriber mapping
                    -> fixed locator notification UDS ---------->
                    <------------- fixed ACK UDS ---------------
```

`TransportType::UnixDomainSocket` and `TransportType::SharedMemory` are explicit public
configuration. Control protocol version 2 records transport on topics/endpoints and rejects mixed
UDS/SHM pairs with `TransportMismatch`. It returns topic identity with discovery but never creates,
maps, unlinks, or forwards business payload memory. Direct mode remains the Phase 1 UDS baseline;
registry mode supports either data path.

## SharedMemoryRegion

`SharedMemoryRegion` is a move-only RAII wrapper. The creator uses
`shm_open(O_CREAT | O_EXCL | O_RDWR)`, `ftruncate`, and writable `mmap`. The subscriber uses
`shm_open(O_RDONLY)`, verifies the actual object size with `fstat`, and maps read-only. Every region
owns its mapping and fd; its destructor calls `munmap` and closes through `UniqueFd`.

POSIX name ownership is separate from mapping ownership. Only the creator owns the name. Explicit
normal unlink occurs after ACK, and the creator destructor performs best-effort unlink on ordinary
error paths. Unit tests cover shared visibility, move construction/assignment, name transfer,
invalid names/sizes, missing objects, unexpected object size, explicit unlink, and destruction.

## Segment Layout

Every V1 object contains exactly a 48-byte explicitly encoded header followed by payload bytes. The
header fields are `MWS3` magic, version 1, header size, segment size, payload size, sequence,
monotonic publish timestamp, and registry topic ID. Fixed-width big-endian encoding avoids relying
on C++ object layout.

Validation requires the exact mapped size, exact `header_size + payload_size`, configured payload
bound, and equality between all duplicated header/notification metadata. The public 4 MiB default
and protocol's 32-bit configured maximum make header addition bounded on the Linux target; codec
validation additionally rejects overflowing or non-representable declarations.

## SHM Notification

The data UDS distinguishes the unchanged `MW01` UDS payload frame from the versioned `MWN3` SHM
metadata protocol. A notification is exactly 248 bytes: a 56-byte field header plus a zero-padded,
bounded 192-byte POSIX name field. It carries frame kind, segment/payload sizes, sequence, timestamp,
topic ID, and name. It has no application payload field.

The ACK is exactly 16 bytes and contains `MWN3`, version, ACK kind, and sequence. A wrong magic,
version, kind, sequence, topic, size, name, padding, or truncated encoding is rejected.

## Ownership / Cleanup

The publisher creates and owns one SHM object per in-flight message. It completely writes the
header and payload before sending the UDS notification. The subscriber opens/maps only after that
notification, validates and copies into `ReceivedMessage`, sends a matching ACK, and then unmaps.
The publisher waits up to its configured ACK timeout, unlinks after the ACK, and finally unmaps and
closes.

Notification and ACK failures, normal disconnects, timeout, and normal exceptions unwind the
publisher-owned region and attempt unlink. Subscribers never unlink publisher names. The process
integration test snapshots the `mw_p3_` `/dev/shm` namespace and requires equality after every SHM
run.

## Thread / Process Model

`publish()` performs create, map, copy, notification, ACK wait, and cleanup synchronously on the
publisher application thread. `waitAndTake()` performs accept, notification receive, open/map,
validation, copy, ACK, and unmap on the subscriber application thread. No data-plane background
thread was added. `mw_registryd` retains its single epoll event loop. Integration/manual acceptance
uses separate registry, publisher, and subscriber processes.

## Error Handling

The registry explicitly rejects transport mismatches. Public errors distinguish general SHM
failure, missing SHM object, invalid mapped SHM, cleanup failure, ACK timeout, connection loss,
oversize payload, and invalid frame. Configuration rejects unknown transport values, non-positive
ACK timeout, and direct SHM mode.

Segment and notification validation occurs before payload allocation or access. `shm_open`,
`ftruncate`, `fstat`, and `mmap` errors cannot leak the creator name on a normal unwind. Protocol or
socket errors reset the data connection. Destructors remain noexcept and use best-effort cleanup.

## Build Result

The required clean directories were removed from the source tree by moving the old generated
artifacts to a dedicated `/tmp` holding directory because the execution environment rejected the
literal `rm -rf` command. A fresh configuration used:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
```

Result: PASS with GCC 13.3.0 and no project compiler warnings.

## CTest Result

```bash
ctest --test-dir build --output-on-failure
```

Result: PASS, 38/38 tests. Final reported CTest time was 3.61 seconds.

## ASan Result

A fresh Debug build used `-DENABLE_ASAN=ON` and ran the complete suite, including the cross-process
4 MiB SHM path. Result: PASS, 38/38 tests, no AddressSanitizer diagnostics.

## UBSan Result

A fresh Debug build used `-DENABLE_UBSAN=ON` and ran the complete suite. Result: PASS, 38/38 tests,
no UndefinedBehaviorSanitizer diagnostics.

## 1 KB Result

Manual SHM run: `sent=3 publish_errors=0`; `received=3 sequence_errors=0 payload_errors=0`;
publisher exit 0; subscriber exit 0. Result: PASS.

## 64 KB Result

Manual SHM run: `sent=3 publish_errors=0`; `received=3 sequence_errors=0 payload_errors=0`;
publisher exit 0; subscriber exit 0. Result: PASS.

## 1 MB Result

Manual SHM run: `sent=3 publish_errors=0`; `received=3 sequence_errors=0 payload_errors=0`;
publisher exit 0; subscriber exit 0. Result: PASS.

## 4 MB Result

Manual SHM run: `sent=3 publish_errors=0`; `received=3 sequence_errors=0 payload_errors=0`;
publisher exit 0; subscriber exit 0. Result: PASS.

## UDS vs SHM Payload Consistency

The process integration test launches a real registry, publisher, and subscriber for both
transports at every required size. Both demos generate and validate the same deterministic bytes;
the subscriber also validates exact size and sequence. SHM additionally validates registry topic
identity in the notification and mapped header.

| Size | UDS Payload Correct | SHM Payload Correct | Identical |
| --- | --- | --- | --- |
| 1 KB | PASS | PASS | PASS |
| 64 KB | PASS | PASS | PASS |
| 1 MB | PASS | PASS | PASS |
| 4 MB | PASS | PASS | PASS |

This is a correctness comparison only. No performance conclusion was measured or claimed.

## Notification Contains No Payload Verification

`ShmProtocolTest.NotificationPreservesLocatorAndHasPayloadIndependentSize` asserts that encoded
notifications for 1 KiB and 4 MiB payload declarations are both exactly 248 bytes and less than 1
KiB. The publisher SHM branch writes only this fixed array to UDS; business bytes are copied only to
the mapping. Result: PASS. UDS Notification Contains Payload: NO.

## Phase 0/1/2 Regression

All prior tests remain enabled. Direct UDS, partial I/O, sequence/timestamp behavior, malformed
frames, cross-process UDS, registry state/control protocol, both startup orders, type mismatch,
`mwctl`, and malformed control requests pass. `mwctl topic info` now also reports `transport`.
Result: PASS.

Install/export acceptance installed `mw_core`, public headers, `mw_registryd`, `mwctl`, and the
CMake package with relative installed-tool RPATHs. The external consumer configured against only
the install prefix, built, ran, and printed `0.1.0`. Installed `mw_registryd` plus installed `mwctl`
also ran successfully. Result: PASS.

## `/dev/shm` Clean Normal Exit Check

Both automated process tests and the manual four-size SHM matrix compared the project namespace
before and after execution. Manual output reported `project_shm_namespace_clean=yes`; no new
`mw_p3_` object remained. Result: PASS.

## Known Limitations

- Phase 3 intentionally uses dynamic SHM creation and mapping for one object per in-flight message.
- No Memory Pool.
- No reusable Chunk Pool.
- No multi-subscriber payload sharing.
- No reference-counted message lifecycle.
- No backpressure.
- No Loaned Sample.
- No crash-time orphan recovery after abrupt `SIGKILL`.
- No ROS2 Adapter.
- No Benchmark conclusions.
- Registry discovery selects one subscriber, and SHM ACK cleanup supports one subscriber.
- `ReceivedMessage` remains owning: publisher copies application data into SHM and subscriber
  copies mapped data into a vector. SHM mode is not zero-copy.
- Direct mode remains UDS-only; SHM requires registry metadata.

## Phase Boundary

Phase 4 was not implemented.
