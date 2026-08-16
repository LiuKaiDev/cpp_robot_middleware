#!/usr/bin/env python3
"""Print a compact summary from committed or demo benchmark aggregates."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]


def median(value: dict[str, float]) -> float:
    return float(value["median"])


def find_case(cases: list[dict[str, Any]], **identity: Any) -> dict[str, Any]:
    return next(case for case in cases if all(case.get(key) == value for key, value in identity.items()))


def print_backpressure(cases: list[dict[str, Any]]) -> None:
    print("policy             delivered_msg/s  drop_rate  overflow  blocked  blocked_ms")
    for policy in ("drop_newest", "drop_oldest", "block_with_timeout"):
        case = find_case(cases, experiment="backpressure", overflow_policy=policy)
        print(
            f"{policy:18} {median(case['messages_per_second']):15.1f} "
            f"{median(case['drop_rate']) * 100:8.2f}% "
            f"{median(case['queue_overflow_count']):9.0f} "
            f"{median(case['blocked_count']):8.0f} "
            f"{median(case['blocked_time_ns']) / 1_000_000:10.1f}"
        )


def print_reference(cases: list[dict[str, Any]]) -> None:
    print("Optimized benchmark reference, 1 publisher -> 1 subscriber medians")
    print("transport  size       latency_p50_us  throughput_MiB/s  messages/s")
    for size in (64, 65536, 1048576, 4194304):
        for transport in ("uds", "shm_copy", "shm_loan", "ros2"):
            latency = find_case(
                cases, experiment="main", transport=transport, profile="latency",
                message_size_bytes=size, subscriber_count=1,
            )
            throughput = find_case(
                cases, experiment="main", transport=transport, profile="throughput",
                message_size_bytes=size, subscriber_count=1,
            )
            print(
                f"{transport:10} {size:9d} {median(latency['latency_p50_ns']) / 1000:15.1f} "
                f"{median(throughput['aggregate_delivered_MB_per_second']):17.1f} "
                f"{median(throughput['messages_per_second']):11.1f}"
            )
    print()
    print_backpressure(cases)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--reference", type=Path,
        default=REPO_ROOT / "benchmark/results/phase8_1_reference/summary.json",
    )
    parser.add_argument("--backpressure-only", action="store_true")
    args = parser.parse_args()
    data = json.loads(args.reference.read_text(encoding="utf-8"))
    if not data.get("all_valid"):
        raise RuntimeError("benchmark aggregate is not fully valid")
    cases = data["cases"]
    if args.backpressure_only:
        print_backpressure(cases)
    else:
        print_reference(cases)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
