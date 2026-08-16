# Codex Task - Phase 7: ROS2 Adapter

## Scope

Implement Phase 7 only: add an external ROS2 adapter package that bridges typed ROS2 messages to
and from the installed middleware core. Preserve the complete Phase 0-6 core, registry, UDS, SHM,
loan/view, heartbeat, and crash-recovery behavior.

## Required Architecture

- Keep `middleware/` and `registry/` entirely independent of ROS2. The dependency direction is
  `mw_ros2_adapter -> installed mw::mw_core`.
- Build the adapter as an independent `ament_cmake` package under `ros2_adapter/`; do not add it to
  the root CMake build.
- Provide `ros2_to_mw_bridge` and `mw_to_ros2_bridge` executables.
- Support `std_msgs/msg/String`, `geometry_msgs/msg/Twist`, and `sensor_msgs/msg/Image` through
  `rclcpp::Serialization<T>` and `rclcpp::SerializedMessage`.
- Use canonical ROS type names plus one stable, versioned adapter wire identifier for middleware
  type matching. Do not claim that it is a complete ROS IDL schema hash.
- Expose registry socket, ROS/MW topics, message type, transport, maximum message size, and useful
  queue/QoS settings as ROS parameters.
- Use the ROS executor callback for ROS2-to-middleware publication. Use a ROS timer and nonblocking
  middleware take for middleware-to-ROS2; do not add a data worker thread or thread pool.
- Use a middleware loan for the SHM transmit path and `SampleView` for the SHM receive path while
  documenting the unavoidable adapter serialization buffer copies.
- Report unsupported types, bad parameters, registry failures, serialization/deserialization
  failures, middleware publish errors, and type mismatches explicitly.

## Verification

- Unit-test String, Twist, and Image serialization round trips, including empty/UTF-8/long String,
  all Twist fields, full Image metadata/data, truncation, wrong ROS payload, and unsupported type.
- Use true process integration tests for all three types in both directions.
- Transfer and byte-check a deterministic 1280x720 RGB8 Image in both directions while remaining
  below the existing 4 MiB middleware limit.
- Run a real `ros2 topic pub --once` acceptance.
- Verify registry state, type mismatch, normal bridge cleanup, and one `SIGKILL` cleanup smoke.
- Use unique ROS and middleware names, an isolated `ROS_DOMAIN_ID`, bounded discovery polling, and
  hard timeouts.
- Retain and pass all 75 core tests, core ASan/UBSan, install/export, and external consumer checks.
- Prove with source search and dynamic-link inspection that `libmw_core.so` has no ROS dependency.
- Build and test the adapter with colcon against the installed middleware package.

## Documentation And Delivery

- Add launch and YAML examples for String, Twist, and Image.
- Add `docs/ROS2_ADAPTER.md`, update `README.md`, and produce `PHASE_7_REPORT.md` with actual results.
- Commit and normally push `main` only after every mandatory acceptance passes.

## Deferred Work

Do not implement a custom RMW, DDS/RTPS integration, dynamic ROS introspection/plugin system,
PointCloud2 support, benchmark infrastructure, profiling, performance comparisons, or Phase 8.
Do not claim that the serialized ROS2 adapter path is end-to-end zero-copy.
