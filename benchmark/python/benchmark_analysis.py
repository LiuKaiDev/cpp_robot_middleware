#!/usr/bin/env python3
"""Deterministic benchmark statistics shared by the runner and analysis tests."""

from __future__ import annotations

import math
import statistics
from typing import Any, Iterable, Sequence


REQUIRED_SUMMARY_FIELDS = {
    "transport",
    "profile",
    "message_size_bytes",
    "subscriber_count",
    "publish_attempts",
    "messages_published",
    "messages_received_total",
    "payload_errors",
    "sequence_errors",
    "latency_sample_count",
    "latency_p50_ns",
    "latency_p90_ns",
    "latency_p99_ns",
    "messages_per_second",
    "publisher_logical_MB_per_second",
    "aggregate_delivered_MB_per_second",
    "publisher_cpu_percent",
    "subscriber_cpu_total_percent",
    "publisher_rss_bytes",
    "drop_count",
    "queue_overflow_count",
    "allocation_failure_count",
    "blocked_count",
    "blocked_time_ns",
    "valid_result",
}


def percentile(values: Sequence[int | float], quantile: float) -> float | None:
    """Return linear interpolation at (n - 1) * quantile on sorted values."""
    if not values:
        return None
    if not 0.0 <= quantile <= 1.0:
        raise ValueError("quantile must be in [0, 1]")
    ordered = sorted(values)
    position = (len(ordered) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return float(ordered[lower])
    fraction = position - lower
    return float(ordered[lower]) + (float(ordered[upper]) - float(ordered[lower])) * fraction


def messages_per_second(message_count: int, elapsed_ns: int) -> float:
    if elapsed_ns <= 0:
        raise ValueError("elapsed_ns must be positive")
    return message_count * 1_000_000_000.0 / elapsed_ns


def mebibytes_per_second(byte_count: int, elapsed_ns: int) -> float:
    return messages_per_second(byte_count, elapsed_ns) / (1024.0 * 1024.0)


def sequence_accounting(sequences: Iterable[int]) -> dict[str, int]:
    gaps = duplicates = out_of_order = 0
    previous: int | None = None
    for sequence in sequences:
        if previous is not None:
            if sequence == previous:
                duplicates += 1
            elif sequence < previous:
                out_of_order += 1
            elif sequence > previous + 1:
                gaps += sequence - previous - 1
        if previous is None or sequence > previous:
            previous = sequence
    return {
        "sequence_gaps": gaps,
        "duplicate_sequences": duplicates,
        "out_of_order_sequences": out_of_order,
        "sequence_errors": duplicates + out_of_order,
    }


def validate_summary(summary: dict[str, Any]) -> list[str]:
    reasons = [f"missing summary field: {field}" for field in sorted(REQUIRED_SUMMARY_FIELDS - summary.keys())]
    if reasons:
        return reasons
    if summary["message_size_bytes"] < 16:
        reasons.append("message_size_bytes is smaller than the benchmark envelope")
    if summary["subscriber_count"] <= 0:
        reasons.append("subscriber_count must be positive")
    if summary["latency_sample_count"] < 0:
        reasons.append("latency_sample_count cannot be negative")
    return reasons


def median_and_range(values: Sequence[int | float]) -> dict[str, float | None]:
    if not values:
        return {"median": None, "min": None, "max": None}
    return {
        "median": float(statistics.median(values)),
        "min": float(min(values)),
        "max": float(max(values)),
    }


def aggregate_repetitions(summaries: Sequence[dict[str, Any]]) -> dict[str, Any]:
    if not summaries:
        raise ValueError("at least one repetition is required")
    identity_fields = ("transport", "profile", "message_size_bytes", "subscriber_count")
    identity = {field: summaries[0][field] for field in identity_fields}
    for summary in summaries[1:]:
        if any(summary[field] != identity[field] for field in identity_fields):
            raise ValueError("cannot aggregate repetitions with different case identity")

    metrics = (
        "publish_attempts",
        "messages_published",
        "messages_received_total",
        "latency_p50_ns",
        "latency_p90_ns",
        "latency_p99_ns",
        "messages_per_second",
        "publisher_logical_MB_per_second",
        "aggregate_delivered_MB_per_second",
        "publisher_cpu_percent",
        "subscriber_cpu_total_percent",
        "publisher_rss_bytes",
        "subscriber_rss_total_bytes",
        "drop_count",
        "drop_rate",
        "queue_overflow_count",
        "allocation_failure_count",
        "blocked_count",
        "blocked_time_ns",
    )
    result: dict[str, Any] = dict(identity)
    result["repetitions_total"] = len(summaries)
    result["repetitions_valid"] = sum(bool(summary.get("valid_result")) for summary in summaries)
    result["valid_result"] = result["repetitions_valid"] == result["repetitions_total"]
    for metric in metrics:
        values = [summary[metric] for summary in summaries if summary.get("valid_result") and summary.get(metric) is not None]
        result[metric] = median_and_range(values)
    result["run_paths"] = [summary.get("run_path", "") for summary in summaries]
    return result
