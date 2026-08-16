#!/usr/bin/env python3
"""Orchestrate bounded cross-process middleware and ROS2 benchmark cases."""

from __future__ import annotations

import argparse
import csv
import json
import os
import pathlib
import random
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any, TextIO

from benchmark_analysis import mebibytes_per_second, messages_per_second, percentile
from collect_machine_info import collect as collect_machine_info


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TRANSPORTS = ("uds", "shm_copy", "shm_loan", "ros2")
PROFILES = ("latency", "throughput")


@dataclass(frozen=True)
class Case:
    transport: str
    profile: str
    message_size: int
    subscriber_count: int
    repetition: int
    experiment: str = "main"
    overflow_policy: str = "block_with_timeout"
    queue_depth: int = 8
    block_timeout_ms: int = 100
    slow_subscriber_us: int = 0


@dataclass
class Child:
    role: str
    index: int
    process: subprocess.Popen[str]
    stdout: TextIO
    stderr: TextIO


def read_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: pathlib.Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def wait_for_path(path: pathlib.Path, timeout: float, children: list[Child] | None = None) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return True
        if children and any(child.process.poll() is not None for child in children):
            return False
        time.sleep(0.02)
    return False


def start_child(
    command: list[str],
    role: str,
    index: int,
    run_dir: pathlib.Path,
    environment: dict[str, str],
) -> Child:
    suffix = f"_{index}" if index >= 0 else ""
    stdout = (run_dir / f"{role}{suffix}.stdout.log").open("w", encoding="utf-8")
    stderr = (run_dir / f"{role}{suffix}.stderr.log").open("w", encoding="utf-8")
    process = subprocess.Popen(
        command,
        cwd=REPO_ROOT,
        env=environment,
        text=True,
        stdout=stdout,
        stderr=stderr,
        start_new_session=True,
    )
    return Child(role, index, process, stdout, stderr)


def terminate_child(child: Child, timeout: float = 3.0) -> None:
    if child.process.poll() is None:
        child.process.send_signal(signal.SIGTERM)
        try:
            child.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            child.process.kill()
            child.process.wait(timeout=timeout)
    child.stdout.close()
    child.stderr.close()


def parse_process_ticks(text: str) -> int:
    fields = text[text.rfind(")") + 2 :].split()
    return int(fields[11]) + int(fields[12])


def process_ticks(pid: int) -> int | None:
    try:
        text = pathlib.Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
        return parse_process_ticks(text)
    except (OSError, ValueError, IndexError):
        return None


def process_rss(pid: int) -> int | None:
    try:
        for line in pathlib.Path(f"/proc/{pid}/status").read_text(encoding="utf-8").splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * 1024
    except (OSError, ValueError, IndexError):
        pass
    return None


def project_shm_snapshot() -> set[str]:
    root = pathlib.Path("/dev/shm")
    if not root.exists():
        return set()
    return {entry.name for entry in root.iterdir() if entry.name.startswith(("mw_p5_", "mw_q5_"))}


def common_command_arguments(case: Case, config: dict[str, Any], run_dir: pathlib.Path, topic: str) -> list[str]:
    rate = int(config["latency_rates_hz"].get(str(case.message_size), config["default_latency_rate_hz"]))
    stride = int(config["latency_sample_stride"][case.profile])
    return [
        "--transport", case.transport,
        "--profile", case.profile,
        "--message-size", str(case.message_size),
        "--subscribers", str(case.subscriber_count),
        "--queue-depth", str(case.queue_depth),
        "--publish-rate-hz", str(rate),
        "--sample-stride", str(stride),
        "--max-samples", str(config["max_latency_samples"]),
        "--slow-subscriber-us", str(case.slow_subscriber_us),
        "--warmup-ms", str(round(config["warmup_seconds"] * 1000)),
        "--measurement-ms", str(round(config["measurement_seconds"] * 1000)),
        "--cooldown-ms", str(round(config["cooldown_seconds"] * 1000)),
        "--readiness-timeout-ms", str(round(config["readiness_timeout_seconds"] * 1000)),
        "--receive-timeout-ms", str(config["receive_timeout_ms"]),
        "--block-timeout-ms", str(case.block_timeout_ms),
        "--overflow-policy", case.overflow_policy,
        "--topic", topic,
        "--run-dir", str(run_dir),
    ]


def executable_paths(args: argparse.Namespace) -> dict[str, pathlib.Path]:
    build = args.build_dir.resolve()
    ros_install = args.ros_install.resolve()
    return {
        "registry": build / "bin" / "mw_registryd",
        "custom_publisher": build / "bin" / "mw_bench_publisher",
        "custom_subscriber": build / "bin" / "mw_bench_subscriber",
        "ros_publisher": ros_install / "mw_ros2_benchmark" / "lib" / "mw_ros2_benchmark" / "mw_ros2_bench_publisher",
        "ros_subscriber": ros_install / "mw_ros2_benchmark" / "lib" / "mw_ros2_benchmark" / "mw_ros2_bench_subscriber",
    }


def monitor_measurement(
    run_dir: pathlib.Path,
    measured: list[Child],
    interval: float,
    timeout: float,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], float]:
    start_marker = run_dir / "measurement.start"
    if not wait_for_path(start_marker, timeout, measured):
        raise RuntimeError("measurement start marker timeout")
    ticks_start = {child.process.pid: process_ticks(child.process.pid) for child in measured}
    start = time.monotonic()
    memory_rows: list[dict[str, Any]] = []

    def sample_memory() -> None:
        elapsed = time.monotonic() - start
        for child in measured:
            rss = process_rss(child.process.pid)
            if rss is not None:
                memory_rows.append({
                    "elapsed_seconds": elapsed,
                    "role": child.role,
                    "index": child.index,
                    "pid": child.process.pid,
                    "rss_bytes": rss,
                })

    sample_memory()
    (run_dir / "measurement.start.ack").write_text("ack\n", encoding="utf-8")
    deadline = start + timeout
    next_memory_sample = start + interval
    while not (run_dir / "measurement.end").exists():
        now = time.monotonic()
        if now >= deadline:
            raise RuntimeError("measurement end marker timeout")
        if any(child.process.poll() is not None for child in measured):
            raise RuntimeError("measured process exited during measurement")
        if now >= next_memory_sample:
            sample_memory()
            next_memory_sample += interval
            continue
        time.sleep(min(0.01, next_memory_sample - now))
    end = time.monotonic()
    sample_memory()
    ticks_end = {child.process.pid: process_ticks(child.process.pid) for child in measured}
    elapsed = end - start
    (run_dir / "measurement.end.ack").write_text("ack\n", encoding="utf-8")

    ticks_per_second = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
    cpu_rows = []
    for child in measured:
        before = ticks_start[child.process.pid]
        after = ticks_end[child.process.pid]
        delta = None if before is None or after is None else max(0, after - before)
        cpu_rows.append({
            "role": child.role,
            "index": child.index,
            "pid": child.process.pid,
            "start_ticks": before,
            "end_ticks": after,
            "delta_ticks": delta,
            "elapsed_seconds": elapsed,
            "cpu_percent": None if delta is None else delta / ticks_per_second / elapsed * 100.0,
        })
    return cpu_rows, memory_rows, elapsed


def write_monitor_files(run_dir: pathlib.Path, cpu_rows: list[dict[str, Any]], memory_rows: list[dict[str, Any]]) -> None:
    with (run_dir / "cpu.csv").open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(cpu_rows[0]) if cpu_rows else ["role"])
        writer.writeheader()
        writer.writerows(cpu_rows)
    with (run_dir / "memory.csv").open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(memory_rows[0]) if memory_rows else ["role"])
        writer.writeheader()
        writer.writerows(memory_rows)


def role_cpu(cpu_rows: list[dict[str, Any]], role: str) -> list[float]:
    return [float(row["cpu_percent"]) for row in cpu_rows if row["role"] == role and row["cpu_percent"] is not None]


def role_rss(memory_rows: list[dict[str, Any]], role: str, index: int | None = None) -> tuple[float, int]:
    values = [
        int(row["rss_bytes"])
        for row in memory_rows
        if row["role"] == role and (index is None or row["index"] == index)
    ]
    if not values:
        return 0.0, 0
    return sum(values) / len(values), max(values)


def shm_pool_classes(queue_depth: int) -> list[dict[str, int]]:
    minimum = max(4, queue_depth + 2)
    defaults = ((256, 32), (4096, 16), (65536, 8), (1048576, 4), (4194304, 2))
    return [
        {"chunk_size": size, "chunk_count": max(count, minimum)}
        for size, count in defaults
    ]


def combine_latency_files(run_dir: pathlib.Path, subscriber_count: int) -> tuple[list[int], list[list[int]]]:
    aggregate: list[int] = []
    per_subscriber: list[list[int]] = []
    with (run_dir / "raw_latency.csv").open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(["subscriber_index", "sequence", "latency_ns"])
        for index in range(subscriber_count):
            latencies: list[int] = []
            path = run_dir / f"subscriber_{index}_latency.csv"
            if path.exists():
                with path.open(encoding="utf-8", newline="") as source:
                    for row in csv.DictReader(source):
                        latency = int(row["latency_ns"])
                        latencies.append(latency)
                        aggregate.append(latency)
                        writer.writerow([index, row["sequence"], latency])
            per_subscriber.append(latencies)
    return aggregate, per_subscriber


def build_summary(
    case: Case,
    config: dict[str, Any],
    run_dir: pathlib.Path,
    cpu_rows: list[dict[str, Any]],
    memory_rows: list[dict[str, Any]],
    exit_status: dict[str, Any],
    initial_reasons: list[str],
) -> dict[str, Any]:
    reasons = list(initial_reasons)
    publisher_path = run_dir / "publisher.json"
    subscriber_paths = [run_dir / f"subscriber_{index}.json" for index in range(case.subscriber_count)]
    if not publisher_path.exists():
        reasons.append("publisher.json is missing")
        publisher = {}
    else:
        publisher = read_json(publisher_path)
    subscribers = []
    for path in subscriber_paths:
        if not path.exists():
            reasons.append(f"{path.name} is missing")
            subscribers.append({})
        else:
            subscribers.append(read_json(path))
    aggregate_latencies, per_subscriber_latencies = combine_latency_files(run_dir, case.subscriber_count)

    elapsed_ns = int(publisher.get("measurement_elapsed_ns", 0))
    elapsed_ns = max(elapsed_ns, 1)
    attempts = int(publisher.get("publish_attempts", 0))
    published = int(publisher.get("messages_published", 0))
    received = [int(value.get("correct_messages_received", 0)) for value in subscribers]
    received_total = sum(received)
    payload_errors = sum(int(value.get("total_payload_errors", 0)) for value in subscribers)
    sequence_errors = sum(int(value.get("sequence_errors", 0)) for value in subscribers)
    sequence_gaps = sum(int(value.get("sequence_gaps", 0)) for value in subscribers)
    omitted = sum(int(value.get("latency_samples_omitted", 0)) for value in subscribers)
    expected_deliveries = attempts * case.subscriber_count
    inferred_drops = max(0, expected_deliveries - received_total)
    reported_drops = int(publisher.get("drop_count", 0))
    drops = max(reported_drops, inferred_drops)

    publisher_cpu = role_cpu(cpu_rows, "publisher")
    subscriber_cpu = role_cpu(cpu_rows, "subscriber")
    publisher_rss_mean, publisher_rss_peak = role_rss(memory_rows, "publisher")
    subscriber_rss = [role_rss(memory_rows, "subscriber", index) for index in range(case.subscriber_count)]
    per_subscriber_p = [
        {
            "subscriber_index": index,
            "latency_sample_count": len(values),
            "latency_p50_ns": percentile(values, 0.50),
            "latency_p90_ns": percentile(values, 0.90),
            "latency_p99_ns": percentile(values, 0.99),
            "messages_received": received[index],
            "messages_per_second": messages_per_second(received[index], elapsed_ns),
            "delivered_MB_per_second": mebibytes_per_second(received[index] * case.message_size, elapsed_ns),
            "cpu_percent": subscriber_cpu[index] if index < len(subscriber_cpu) else None,
            "rss_mean_bytes": subscriber_rss[index][0],
            "rss_peak_bytes": subscriber_rss[index][1],
        }
        for index, values in enumerate(per_subscriber_latencies)
    ]

    configured_ns = int(config["measurement_seconds"] * 1_000_000_000)
    if elapsed_ns < configured_ns * 0.90:
        reasons.append("measurement duration was incomplete")
    if payload_errors:
        reasons.append(f"payload_errors={payload_errors}")
    if sequence_errors:
        reasons.append(f"sequence_errors={sequence_errors}")
    if omitted:
        reasons.append(f"bounded latency sample storage overflowed by {omitted}")
    if not aggregate_latencies:
        reasons.append("no latency samples were collected")
    for role, status in exit_status.items():
        if role != "registry" and status != 0:
            reasons.append(f"{role} exit status was {status}")

    summary = {
        "schema_version": 1,
        "experiment": case.experiment,
        "transport": case.transport,
        "profile": case.profile,
        "message_size_bytes": case.message_size,
        "subscriber_count": case.subscriber_count,
        "warmup_seconds": config["warmup_seconds"],
        "measurement_seconds": config["measurement_seconds"],
        "cooldown_seconds": config["cooldown_seconds"],
        "publish_rate": int(config["latency_rates_hz"].get(str(case.message_size), config["default_latency_rate_hz"])) if case.profile == "latency" else None,
        "queue_depth": case.queue_depth,
        "overflow_policy": case.overflow_policy,
        "block_timeout_ms": case.block_timeout_ms,
        "shm_pool_classes": shm_pool_classes(case.queue_depth) if case.transport.startswith("shm_") else None,
        "ros_qos": {"reliability": "reliable", "history": "keep_last", "depth": case.queue_depth} if case.transport == "ros2" else None,
        "measurement_elapsed_ns": elapsed_ns,
        "publish_attempts": attempts,
        "messages_published": published,
        "messages_received": received,
        "messages_received_total": received_total,
        "payload_errors": payload_errors,
        "sequence_errors": sequence_errors,
        "sequence_gaps": sequence_gaps,
        "latency_sample_stride": config["latency_sample_stride"][case.profile],
        "latency_sample_count": len(aggregate_latencies),
        "latency_p50_ns": percentile(aggregate_latencies, 0.50),
        "latency_p90_ns": percentile(aggregate_latencies, 0.90),
        "latency_p99_ns": percentile(aggregate_latencies, 0.99),
        "worst_subscriber_p99_ns": max((value["latency_p99_ns"] for value in per_subscriber_p if value["latency_p99_ns"] is not None), default=None),
        "per_subscriber": per_subscriber_p,
        "messages_per_second": messages_per_second(received_total, elapsed_ns) / case.subscriber_count,
        "aggregate_delivered_messages_per_second": messages_per_second(received_total, elapsed_ns),
        "publisher_logical_MB_per_second": mebibytes_per_second(published * case.message_size, elapsed_ns),
        "aggregate_delivered_MB_per_second": mebibytes_per_second(received_total * case.message_size, elapsed_ns),
        "publisher_cpu_percent": publisher_cpu[0] if publisher_cpu else None,
        "subscriber_cpu_percent": subscriber_cpu,
        "subscriber_cpu_total_percent": sum(subscriber_cpu),
        "publisher_rss_bytes": publisher_rss_peak,
        "publisher_rss_mean_bytes": publisher_rss_mean,
        "subscriber_rss_bytes": [value[1] for value in subscriber_rss],
        "subscriber_rss_mean_bytes": [value[0] for value in subscriber_rss],
        "subscriber_rss_total_bytes": sum(value[1] for value in subscriber_rss),
        "drop_count": drops,
        "drop_rate": drops / expected_deliveries if expected_deliveries else 0.0,
        "queue_overflow_count": int(publisher.get("queue_overflow_count", 0)),
        "allocation_failure_count": int(publisher.get("allocation_failure_count", 0)),
        "blocked_count": int(publisher.get("blocked_count", 0)),
        "blocked_time_ns": int(publisher.get("blocked_time_ns", 0)),
        "exit_status": exit_status,
        "invalid_reasons": reasons,
        "valid_result": not reasons,
        "run_path": str(run_dir),
    }
    write_json(run_dir / "summary.json", summary)
    return summary


def run_directory(root: pathlib.Path, case: Case) -> pathlib.Path:
    if case.experiment == "backpressure":
        return root / "backpressure" / case.overflow_policy / f"run_{case.repetition:02d}"
    return (
        root
        / case.transport
        / str(case.message_size)
        / f"1_to_{case.subscriber_count}"
        / case.profile
        / f"run_{case.repetition:02d}"
    )


def run_case(
    case: Case,
    config: dict[str, Any],
    run_root: pathlib.Path,
    paths: dict[str, pathlib.Path],
    environment: dict[str, str],
    case_number: int,
) -> dict[str, Any]:
    run_dir = run_directory(run_root, case)
    run_dir.mkdir(parents=True, exist_ok=False)
    token = f"{os.getpid():x}{case_number:x}{case.repetition:x}"
    topic = f"/mw_benchmark/r{token}"
    registry_path = pathlib.Path(f"/tmp/mwr_{token}.sock")
    publisher_socket = pathlib.Path(f"/tmp/mwbp_{token}.sock")
    subscriber_sockets = [pathlib.Path(f"/tmp/mwbs_{token}_{index}.sock") for index in range(case.subscriber_count)]
    write_json(run_dir / "case_config.json", {**case.__dict__, "topic": topic})

    before_shm = project_shm_snapshot()
    children: list[Child] = []
    measured: list[Child] = []
    cpu_rows: list[dict[str, Any]] = []
    memory_rows: list[dict[str, Any]] = []
    reasons: list[str] = []
    exit_status: dict[str, Any] = {}
    case_environment = dict(environment)
    if case.transport == "ros2":
        case_environment["RMW_IMPLEMENTATION"] = "rmw_fastrtps_cpp"
        case_environment["ROS_DOMAIN_ID"] = str(config["ros_domain_id"])
        case_environment["ROS_AUTOMATIC_DISCOVERY_RANGE"] = "LOCALHOST"

    common = common_command_arguments(case, config, run_dir, topic)
    try:
        if case.transport != "ros2":
            registry = start_child(
                [str(paths["registry"]), "--socket", str(registry_path)],
                "registry",
                -1,
                run_dir,
                case_environment,
            )
            children.append(registry)
            if not wait_for_path(registry_path, config["readiness_timeout_seconds"], [registry]):
                raise RuntimeError("registry readiness timeout")

        for index in range(case.subscriber_count):
            command = [str(paths["ros_subscriber"] if case.transport == "ros2" else paths["custom_subscriber"]), *common, "--subscriber-index", str(index)]
            if case.transport != "ros2":
                command.extend(["--registry", str(registry_path), "--socket", str(subscriber_sockets[index])])
            child = start_child(command, "subscriber", index, run_dir, case_environment)
            children.append(child)
            measured.append(child)
        for index in range(case.subscriber_count):
            if not wait_for_path(run_dir / f"subscriber_{index}.ready", config["readiness_timeout_seconds"], measured):
                raise RuntimeError(f"subscriber {index} readiness timeout")

        command = [str(paths["ros_publisher"] if case.transport == "ros2" else paths["custom_publisher"]), *common]
        if case.transport != "ros2":
            command.extend(["--registry", str(registry_path), "--socket", str(publisher_socket)])
        publisher = start_child(command, "publisher", -1, run_dir, case_environment)
        children.append(publisher)
        measured.insert(0, publisher)

        measurement_timeout = (
            config["warmup_seconds"]
            + config["measurement_seconds"]
            + config["cooldown_seconds"]
            + config["readiness_timeout_seconds"] * 2
        )
        cpu_rows, memory_rows, _ = monitor_measurement(
            run_dir, measured, config["monitor_interval_seconds"], measurement_timeout
        )
        write_monitor_files(run_dir, cpu_rows, memory_rows)

        publisher.process.wait(timeout=config["cooldown_seconds"] + config["shutdown_timeout_seconds"])
        exit_status["publisher"] = publisher.process.returncode
        for child in [value for value in measured if value.role == "subscriber"]:
            child.process.wait(timeout=config["shutdown_timeout_seconds"])
            exit_status[f"subscriber_{child.index}"] = child.process.returncode
    except (OSError, RuntimeError, subprocess.TimeoutExpired, ValueError) as error:
        reasons.append(str(error))
    finally:
        for child in reversed(children):
            terminate_child(child, config["shutdown_timeout_seconds"])
            if child.role == "registry":
                exit_status["registry"] = child.process.returncode
        for path in [registry_path, publisher_socket, *subscriber_sockets]:
            if path.exists():
                reasons.append(f"socket resource remained after cleanup: {path.name}")
        leaked_shm = project_shm_snapshot() - before_shm
        if leaked_shm:
            reasons.append("SHM resources remained after cleanup: " + ",".join(sorted(leaked_shm)))
    if not (run_dir / "cpu.csv").exists():
        write_monitor_files(run_dir, cpu_rows, memory_rows)
    return build_summary(case, config, run_dir, cpu_rows, memory_rows, exit_status, reasons)


def build_cases(config: dict[str, Any], args: argparse.Namespace) -> list[Case]:
    transports = [args.transport] if args.transport else list(config["transports"])
    if args.skip_ros2:
        transports = [value for value in transports if value != "ros2"]
    sizes = [args.message_size] if args.message_size else list(config["message_sizes"])
    subscribers = [args.subscribers] if args.subscribers else list(config["subscriber_counts"])
    profiles = [args.profile] if args.profile else list(config["profiles"])
    repetitions = args.repetitions or int(config["repetitions"])
    seed = int(config["ordering_seed"])
    cases: list[Case] = []
    base_index = 0
    for repetition in range(1, repetitions + 1):
        for size in sizes:
            for count in subscribers:
                for profile in profiles:
                    ordered = list(transports)
                    random.Random(seed + base_index).shuffle(ordered)
                    base_index += 1
                    for transport in ordered:
                        cases.append(Case(transport, profile, size, count, repetition, queue_depth=int(config["queue_depth"]), block_timeout_ms=int(config["block_timeout_ms"])))
    if not args.skip_backpressure and config.get("backpressure", {}).get("enabled") and not any((args.transport, args.message_size, args.subscribers, args.profile)):
        backpressure = config["backpressure"]
        for repetition in range(1, repetitions + 1):
            for policy in backpressure["policies"]:
                cases.append(Case(
                    "shm_copy",
                    "throughput",
                    int(backpressure["message_size"]),
                    int(backpressure["subscriber_count"]),
                    repetition,
                    experiment="backpressure",
                    overflow_policy=policy,
                    queue_depth=int(backpressure["queue_depth"]),
                    block_timeout_ms=int(backpressure["block_timeout_ms"]),
                    slow_subscriber_us=int(backpressure["slow_subscriber_us"]),
                ))
    return cases


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=pathlib.Path, default=REPO_ROOT / "benchmark/configs/smoke.json")
    parser.add_argument("--results-root", type=pathlib.Path, default=REPO_ROOT / "benchmark/results")
    parser.add_argument("--build-dir", type=pathlib.Path, default=REPO_ROOT / "build_release")
    parser.add_argument("--ros-install", type=pathlib.Path, default=REPO_ROOT / "install_ros2_benchmark")
    parser.add_argument("--run-id")
    parser.add_argument("--transport", choices=TRANSPORTS)
    parser.add_argument("--profile", choices=PROFILES)
    parser.add_argument("--message-size", type=int)
    parser.add_argument("--subscribers", type=int, choices=(1, 2, 4))
    parser.add_argument("--repetitions", type=int)
    parser.add_argument("--skip-ros2", action="store_true")
    parser.add_argument("--skip-backpressure", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-plots", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    config = read_json(args.config.resolve())
    cases = build_cases(config, args)
    if not cases:
        raise ValueError("filters selected no benchmark cases")
    if args.dry_run:
        print(json.dumps({"case_count": len(cases), "cases": [case.__dict__ for case in cases]}, indent=2))
        return 0

    paths = executable_paths(args)
    required = {"registry", "custom_publisher", "custom_subscriber"}
    if any(case.transport == "ros2" for case in cases):
        required |= {"ros_publisher", "ros_subscriber"}
    missing = [str(paths[name]) for name in sorted(required) if not paths[name].is_file()]
    if missing:
        raise FileNotFoundError("missing benchmark executable(s): " + ", ".join(missing))

    short_commit = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"], cwd=REPO_ROOT, check=True, capture_output=True, text=True
    ).stdout.strip()
    run_id = args.run_id or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + f"_{short_commit}"
    run_root = args.results_root.resolve() / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    effective_config = dict(config)
    effective_config["run_id"] = run_id
    effective_config["case_count"] = len(cases)
    effective_config["case_order"] = [case.__dict__ for case in cases]
    write_json(run_root / "config.json", effective_config)

    environment = dict(os.environ)
    environment["RMW_IMPLEMENTATION"] = "rmw_fastrtps_cpp"
    write_json(run_root / "machine.json", collect_machine_info(REPO_ROOT, "Release", effective_config))
    summaries = []
    for index, case in enumerate(cases, start=1):
        print(
            f"[{index}/{len(cases)}] {case.experiment} {case.transport} {case.profile} "
            f"{case.message_size}B 1->{case.subscriber_count} rep={case.repetition}",
            flush=True,
        )
        summaries.append(run_case(case, config, run_root, paths, environment, index))

    analysis = subprocess.run([sys.executable, str(REPO_ROOT / "benchmark/python/analyze_results.py"), str(run_root)], cwd=REPO_ROOT, check=False)
    if not args.no_plots:
        subprocess.run([sys.executable, str(REPO_ROOT / "benchmark/python/plot_results.py"), str(run_root)], cwd=REPO_ROOT, check=True)
    valid = sum(bool(summary["valid_result"]) for summary in summaries)
    print(json.dumps({"results_root": str(run_root), "valid_runs": valid, "total_runs": len(summaries)}))
    return 0 if valid == len(summaries) and analysis.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
