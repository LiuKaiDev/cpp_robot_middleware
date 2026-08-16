#!/usr/bin/env python3
"""Run the Phase 8.1 representative matrix with external /proc observation."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import random
import shutil
import signal
import subprocess
import sys
import time
from collections import Counter
from dataclasses import dataclass
from typing import Any


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CUSTOM_SCENARIOS = (
    (64, 1),
    (4096, 1),
    (65536, 1),
    (1048576, 1),
    (4194304, 1),
    (65536, 4),
    (4194304, 4),
)
ROS2_SCENARIOS = ((65536, 1), (4194304, 1))
PROFILES = ("latency", "throughput")
CUSTOM_TRANSPORTS = ("uds", "shm_copy", "shm_loan")


@dataclass(frozen=True)
class ProfileCase:
    transport: str
    profile: str
    message_size: int
    subscriber_count: int

    @property
    def run_id(self) -> str:
        return (
            f"{self.transport}_{self.profile}_{self.message_size}_"
            f"1to{self.subscriber_count}"
        )


def read_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: pathlib.Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def parse_stat(text: str) -> dict[str, int]:
    fields = text[text.rfind(")") + 2 :].split()
    return {
        "ppid": int(fields[1]),
        "minor_faults": int(fields[7]),
        "major_faults": int(fields[9]),
        "cpu_ticks": int(fields[11]) + int(fields[12]),
    }


def process_snapshot(pid: int) -> dict[str, Any] | None:
    try:
        stat = parse_stat(pathlib.Path(f"/proc/{pid}/stat").read_text(encoding="utf-8"))
        status: dict[str, int] = {}
        for line in pathlib.Path(f"/proc/{pid}/status").read_text(encoding="utf-8").splitlines():
            if line.startswith("voluntary_ctxt_switches:"):
                status["voluntary_context_switches"] = int(line.split()[1])
            elif line.startswith("nonvoluntary_ctxt_switches:"):
                status["nonvoluntary_context_switches"] = int(line.split()[1])
        return {**stat, **status}
    except (OSError, ValueError, IndexError):
        return None


def process_identity(pid: int) -> dict[str, Any] | None:
    try:
        arguments = pathlib.Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0")
    except OSError:
        return None
    decoded = [value.decode(errors="replace") for value in arguments if value]
    if not decoded:
        return None
    executable = pathlib.Path(decoded[0]).name
    if executable in {"mw_bench_publisher", "mw_ros2_bench_publisher"}:
        return {"role": "publisher", "index": -1, "executable": executable}
    if executable in {"mw_bench_subscriber", "mw_ros2_bench_subscriber"}:
        index = -1
        if "--subscriber-index" in decoded:
            position = decoded.index("--subscriber-index")
            index = int(decoded[position + 1])
        return {"role": "subscriber", "index": index, "executable": executable}
    if executable == "mw_registryd":
        return {"role": "registry", "index": -1, "executable": executable}
    return None


def descendants(root_pid: int) -> set[int]:
    parent_by_pid: dict[int, int] = {}
    for stat_path in pathlib.Path("/proc").glob("[0-9]*/stat"):
        try:
            pid = int(stat_path.parent.name)
            parent_by_pid[pid] = parse_stat(stat_path.read_text(encoding="utf-8"))["ppid"]
        except (OSError, ValueError, IndexError):
            continue
    result: set[int] = set()
    frontier = {root_pid}
    while frontier:
        children = {pid for pid, parent in parent_by_pid.items() if parent in frontier}
        children -= result
        if not children:
            break
        result |= children
        frontier = children
    return result


def measured_processes(root_pid: int) -> dict[int, dict[str, Any]]:
    result: dict[int, dict[str, Any]] = {}
    for pid in descendants(root_pid):
        identity = process_identity(pid)
        if identity is not None:
            result[pid] = identity
    return result


def wait_for_single_marker(
    run_root: pathlib.Path,
    name: str,
    process: subprocess.Popen[str],
    timeout: float,
) -> pathlib.Path:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        markers = list(run_root.rglob(name)) if run_root.exists() else []
        if len(markers) == 1:
            return markers[0]
        if len(markers) > 1:
            raise RuntimeError(f"multiple {name} markers found below {run_root}")
        if process.poll() is not None:
            raise RuntimeError(f"benchmark runner exited before {name}: {process.returncode}")
        time.sleep(0.01)
    raise RuntimeError(f"timeout waiting for {name}")


def read_wchan(pid: int) -> str:
    try:
        value = pathlib.Path(f"/proc/{pid}/wchan").read_text(encoding="utf-8").strip()
        return "running" if value in {"", "0"} else value
    except OSError:
        return "unavailable"


def profile_runner_process(
    process: subprocess.Popen[str],
    run_root: pathlib.Path,
) -> dict[str, Any]:
    start_marker = wait_for_single_marker(run_root, "measurement.start", process, 30.0)
    processes = measured_processes(process.pid)
    expected_subscribers = int(run_root.name.rsplit("1to", 1)[1])
    role_count = Counter(value["role"] for value in processes.values())
    if role_count["publisher"] != 1 or role_count["subscriber"] != expected_subscribers:
        raise RuntimeError(f"unexpected measured process set: {processes}")

    start = {pid: process_snapshot(pid) for pid in processes}
    wchan_counts = {pid: Counter() for pid in processes}
    samples = 0
    end_marker = start_marker.parent / "measurement.end"
    while not end_marker.exists():
        if process.poll() is not None:
            raise RuntimeError("benchmark runner exited during measurement")
        for pid in processes:
            wchan_counts[pid][read_wchan(pid)] += 1
        samples += 1
        time.sleep(0.1)
    end = {pid: process_snapshot(pid) for pid in processes}

    rows = []
    counter_fields = (
        "cpu_ticks",
        "minor_faults",
        "major_faults",
        "voluntary_context_switches",
        "nonvoluntary_context_switches",
    )
    for pid, identity in sorted(
        processes.items(), key=lambda value: (value[1]["role"], value[1]["index"])
    ):
        before = start[pid]
        after = end[pid]
        deltas = {}
        if before is not None and after is not None:
            for field in counter_fields:
                if field in before and field in after:
                    deltas[field] = max(0, int(after[field]) - int(before[field]))
        rows.append(
            {
                "pid": pid,
                **identity,
                "start": before,
                "end": after,
                "deltas": deltas,
                "wchan_sample_count": samples,
                "wchan_counts": dict(sorted(wchan_counts[pid].items())),
            }
        )
    return {
        "schema_version": 1,
        "observer": "procfs-boundary-snapshot-plus-100ms-wchan-sampling",
        "processes": rows,
    }


def build_cases() -> list[ProfileCase]:
    cases = [
        ProfileCase(transport, profile, message_size, subscriber_count)
        for message_size, subscriber_count in CUSTOM_SCENARIOS
        for profile in PROFILES
        for transport in CUSTOM_TRANSPORTS
    ]
    cases.extend(
        ProfileCase("ros2", profile, message_size, subscriber_count)
        for message_size, subscriber_count in ROS2_SCENARIOS
        for profile in PROFILES
    )
    random.Random(8101).shuffle(cases)
    return cases


def run_case(
    case: ProfileCase,
    output_root: pathlib.Path,
    build_dir: pathlib.Path,
    ros_install: pathlib.Path,
) -> dict[str, Any]:
    command = [
        sys.executable,
        str(REPO_ROOT / "benchmark/python/run_benchmarks.py"),
        "--config",
        str(REPO_ROOT / "benchmark/configs/full.json"),
        "--results-root",
        str(output_root),
        "--build-dir",
        str(build_dir),
        "--ros-install",
        str(ros_install),
        "--run-id",
        case.run_id,
        "--transport",
        case.transport,
        "--profile",
        case.profile,
        "--message-size",
        str(case.message_size),
        "--subscribers",
        str(case.subscriber_count),
        "--repetitions",
        "1",
        "--skip-backpressure",
        "--no-plots",
    ]
    run_root = output_root / case.run_id
    process = subprocess.Popen(command, cwd=REPO_ROOT, text=True, start_new_session=True)
    try:
        observation = profile_runner_process(process, run_root)
        return_code = process.wait(timeout=30.0)
    except BaseException:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5.0)
        raise
    if return_code != 0:
        raise RuntimeError(f"benchmark runner failed for {case.run_id}: {return_code}")
    write_json(run_root / "proc_profile.json", observation)
    aggregate = read_json(run_root / "aggregated/summary.json")
    if not aggregate["all_valid"] or aggregate["valid_run_count"] != 1:
        raise RuntimeError(f"invalid benchmark result for {case.run_id}")
    return {
        **case.__dict__,
        "run_id": case.run_id,
        "valid": True,
        "process_count": len(observation["processes"]),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-root", type=pathlib.Path, required=True)
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    parser.add_argument("--ros-install", type=pathlib.Path, required=True)
    parser.add_argument("--limit", type=int)
    args = parser.parse_args()

    output_root = args.output_root.resolve()
    output_root.mkdir(parents=True, exist_ok=False)
    cases = build_cases()
    if args.limit is not None:
        if args.limit <= 0:
            raise ValueError("--limit must be positive")
        cases = cases[: args.limit]
    manifest = {
        "schema_version": 1,
        "git_revision": subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip(),
        "git_dirty": bool(
            subprocess.run(
                ["git", "status", "--porcelain"],
                cwd=REPO_ROOT,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
        ),
        "perf": shutil.which("perf"),
        "strace": shutil.which("strace"),
        "perf_event_paranoid": pathlib.Path("/proc/sys/kernel/perf_event_paranoid").read_text().strip(),
        "kptr_restrict": pathlib.Path("/proc/sys/kernel/kptr_restrict").read_text().strip(),
        "observer": "procfs-boundary-snapshot-plus-100ms-wchan-sampling",
        "case_order": [case.__dict__ for case in cases],
        "results": [],
    }
    write_json(output_root / "profile_manifest.json", manifest)
    for index, case in enumerate(cases, start=1):
        print(f"[{index}/{len(cases)}] {case.run_id}", flush=True)
        manifest["results"].append(
            run_case(case, output_root, args.build_dir.resolve(), args.ros_install.resolve())
        )
        write_json(output_root / "profile_manifest.json", manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
