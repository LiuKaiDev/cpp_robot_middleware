#!/usr/bin/env python3
"""Generate the mandatory deterministic PNG plots from aggregated summaries."""

from __future__ import annotations

import argparse
import json
import pathlib

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402


TRANSPORT_ORDER = ("uds", "shm_copy", "shm_loan", "ros2")
COLORS = {"uds": "#3b6ea8", "shm_copy": "#298f67", "shm_loan": "#b45f38", "ros2": "#6d597a"}


def median(case: dict, metric: str) -> float | None:
    value = case.get(metric, {})
    return value.get("median") if isinstance(value, dict) else None


def save_plot(path: pathlib.Path, xlabel: str, ylabel: str, log_x: bool = False) -> None:
    if log_x:
        plt.xscale("log", base=2)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.grid(True, alpha=0.25)
    handles, labels = plt.gca().get_legend_handles_labels()
    if handles:
        plt.legend(handles, labels)
    plt.tight_layout()
    plt.savefig(path, dpi=160)
    plt.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run_root", type=pathlib.Path)
    args = parser.parse_args()
    aggregate_path = args.run_root / "aggregated" / "summary.json"
    result = json.loads(aggregate_path.read_text(encoding="utf-8"))
    cases = [case for case in result["cases"] if case["experiment"] == "main" and case["valid_result"]]
    output = aggregate_path.parent
    available_subscribers = sorted({case["subscriber_count"] for case in cases})
    primary_subscriber_count = 1 if 1 in available_subscribers else available_subscribers[0]

    plt.figure(figsize=(9, 5.5))
    for transport in TRANSPORT_ORDER:
        selected = sorted(
            (case for case in cases if case["transport"] == transport and case["profile"] == "latency" and case["subscriber_count"] == primary_subscriber_count),
            key=lambda case: case["message_size_bytes"],
        )
        if selected:
            sizes = [case["message_size_bytes"] for case in selected]
            plt.plot(sizes, [median(case, "latency_p50_ns") / 1000.0 for case in selected], marker="o", color=COLORS[transport], label=f"{transport} p50")
            plt.plot(sizes, [median(case, "latency_p99_ns") / 1000.0 for case in selected], marker="x", linestyle="--", color=COLORS[transport], label=f"{transport} p99")
    save_plot(output / "latency_vs_message_size.png", "Application payload bytes", "Latency (us)", True)

    plt.figure(figsize=(9, 5.5))
    for transport in TRANSPORT_ORDER:
        selected = sorted(
            (case for case in cases if case["transport"] == transport and case["profile"] == "throughput" and case["subscriber_count"] == primary_subscriber_count),
            key=lambda case: case["message_size_bytes"],
        )
        if selected:
            plt.plot([case["message_size_bytes"] for case in selected], [median(case, "aggregate_delivered_MB_per_second") for case in selected], marker="o", color=COLORS[transport], label=transport)
    save_plot(output / "throughput_vs_message_size.png", "Application payload bytes", "Aggregate delivered MiB/s", True)

    plt.figure(figsize=(9, 5.5))
    for transport in TRANSPORT_ORDER:
        selected = sorted(
            (case for case in cases if case["transport"] == transport and case["profile"] == "throughput" and case["subscriber_count"] == primary_subscriber_count),
            key=lambda case: case["message_size_bytes"],
        )
        if selected:
            cpu = [median(case, "publisher_cpu_percent") + median(case, "subscriber_cpu_total_percent") for case in selected]
            plt.plot([case["message_size_bytes"] for case in selected], cpu, marker="o", color=COLORS[transport], label=transport)
    save_plot(output / "cpu_vs_message_size.png", "Application payload bytes", "Publisher + subscriber CPU (%)", True)

    target_size = 65536
    available_sizes = sorted({case["message_size_bytes"] for case in cases if case["profile"] == "throughput"})
    if available_sizes:
        target_size = min(available_sizes, key=lambda value: abs(value - target_size))
    plt.figure(figsize=(9, 5.5))
    for transport in TRANSPORT_ORDER:
        selected = sorted(
            (case for case in cases if case["transport"] == transport and case["profile"] == "throughput" and case["message_size_bytes"] == target_size),
            key=lambda case: case["subscriber_count"],
        )
        if selected:
            plt.plot([case["subscriber_count"] for case in selected], [median(case, "aggregate_delivered_MB_per_second") for case in selected], marker="o", color=COLORS[transport], label=transport)
    save_plot(output / "subscriber_count_vs_throughput.png", "Subscriber count", f"Aggregate delivered MiB/s ({target_size} B)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
