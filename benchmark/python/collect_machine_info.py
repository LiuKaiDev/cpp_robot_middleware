#!/usr/bin/env python3
"""Collect reproducibility metadata without recording user, home, or hostname."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import platform
import subprocess
from datetime import datetime, timezone
from typing import Any


def command_output(command: list[str], cwd: pathlib.Path | None = None) -> str:
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        ).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return ""


def cpu_model() -> str:
    try:
        for line in pathlib.Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor()


def memory_bytes() -> int:
    try:
        for line in pathlib.Path("/proc/meminfo").read_text(encoding="utf-8").splitlines():
            if line.startswith("MemTotal:"):
                return int(line.split()[1]) * 1024
    except (OSError, ValueError, IndexError):
        pass
    return 0


def collect(repo: pathlib.Path, build_type: str, config: dict[str, Any]) -> dict[str, Any]:
    release_flags = ""
    cache = repo / "build_release" / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_CXX_FLAGS_RELEASE:"):
                release_flags = line.split("=", 1)[-1]
                break
    return {
        "captured_at_utc": datetime.now(timezone.utc).isoformat(),
        "os": platform.system(),
        "os_release": platform.release(),
        "kernel": platform.version(),
        "wsl": "microsoft" in platform.release().lower() or "WSL_INTEROP" in os.environ,
        "architecture": platform.machine(),
        "cpu_model": cpu_model(),
        "logical_cpu_count": os.cpu_count(),
        "memory_bytes": memory_bytes(),
        "compiler": command_output(["g++", "--version"]).splitlines()[0],
        "cmake": command_output(["cmake", "--version"]).splitlines()[0],
        "python": platform.python_version(),
        "build_type": build_type,
        "release_flags": release_flags,
        "git_commit": command_output(["git", "rev-parse", "HEAD"], repo),
        "git_dirty": bool(command_output(["git", "status", "--porcelain"], repo)),
        "ros_distro": os.environ.get("ROS_DISTRO", ""),
        "rmw_implementation": os.environ.get("RMW_IMPLEMENTATION", ""),
        "ros2_prefix": command_output(["ros2", "pkg", "prefix", "rclcpp"]),
        "benchmark_configuration": config,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--config", type=pathlib.Path, required=True)
    parser.add_argument("--build-type", default="Release")
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8"))
    args.output.write_text(
        json.dumps(collect(args.repo.resolve(), args.build_type, config), indent=2) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
