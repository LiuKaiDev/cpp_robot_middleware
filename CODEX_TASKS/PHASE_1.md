# Codex Task - Phase 1: Unix Domain Socket Pub/Sub Baseline

## Scope

Implement Phase 1 only: a Linux-local, copied-payload baseline with one publisher, one subscriber,
one topic, and an explicitly configured Unix socket pathname. The subscriber is the UDS server and
the publisher is the UDS client.

## Required API And Behavior

- Add `Context`, `Publisher`, `Subscriber`, `PublisherConfig`, `SubscriberConfig`, `PublishResult`,
  and an owning `ReceivedMessage`.
- `Publisher::publish()` sends a length-prefixed frame on the application thread.
- `Subscriber::take()` is nonblocking; `waitAndTake()` waits with a caller-supplied timeout.
- Use a fixed-width frame containing magic, payload size, per-publisher sequence, and monotonic
  publish timestamp. Encode fields explicitly rather than sending C++ structure padding.
- Support 0 B, 64 B, 1 KB, and 1 MB payloads.
- Handle partial reads/writes, `EINTR`, peer closure, invalid magic, oversize, truncated frames,
  disconnect, and a later publisher reconnect.
- Suppress `SIGPIPE` on publisher writes.
- Own file descriptors through move-only RAII. The subscriber owns and removes a pathname that it
  successfully bound during clean shutdown.
- Do not add runtime worker threads.

## Tests And Programs

- Unit-test frame encode/decode/validation and forced partial/EINTR I/O loops.
- Integration-test the actual Publisher/Subscriber path, required message sizes, strict sequence,
  timeout, invalid/truncated input, disconnect/reconnect, and peer closure.
- Include a genuine cross-process integration test.
- Build `mw_ping_publisher` and `mw_ping_subscriber` with `--socket`, `--count`, and `--size`.
- Use deterministic payload bytes derived from sequence and offset.
- Keep CTest fast; run 100000 messages as a separate acceptance command.
- Run a clean Debug build, CTest, install/export consumer, ASan/UBSan smoke test, 100000 messages at
  64 B, and additional 1 KB and 1 MB transfers.

## Documentation And Delivery

- Update `README.md` to Phase 1 status and explicitly list exclusions.
- Add `docs/UDS_BASELINE.md` and `PHASE_1_REPORT.md` with actual commands/results.
- Run `git diff --check`, inspect the final diff/status, and ensure generated artifacts are ignored.
- Commit and push `main` only after all configure, build, test, acceptance, documentation, and report
  checks pass. Never force-push or rewrite history.

## Prohibited Phase 2+ Work

Do not implement registry/discovery, topic or node registration, `mw_registryd`, `mwctl`, shared
memory, `mmap`, memory pools/chunks, reference counting, subscriber queues, backpressure policies,
loaned samples, heartbeat/crash recovery, `eventfd`, `epoll` optimization, `SCM_RIGHTS`, ROS2, a
benchmark framework, lock-free queues, or multi-publisher topics.
