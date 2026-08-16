from __future__ import annotations

import json
import pathlib
import types
import unittest

from benchmark_analysis import (
    aggregate_repetitions,
    mebibytes_per_second,
    percentile,
    sequence_accounting,
    validate_summary,
)
from run_benchmarks import build_cases, parse_process_ticks, shm_pool_classes


class AnalysisTest(unittest.TestCase):
    def test_percentiles_use_documented_linear_interpolation(self) -> None:
        values = [0, 10, 20, 30, 40]
        self.assertEqual(percentile(values, 0.50), 20.0)
        self.assertEqual(percentile(values, 0.90), 36.0)
        self.assertEqual(percentile(values, 0.99), 39.6)
        self.assertIsNone(percentile([], 0.50))

    def test_throughput_uses_binary_mebibytes(self) -> None:
        self.assertEqual(mebibytes_per_second(1024 * 1024, 1_000_000_000), 1.0)

    def test_sequence_accounting_separates_gaps_from_corruption(self) -> None:
        result = sequence_accounting([1, 3, 3, 2, 4])
        self.assertEqual(result["sequence_gaps"], 1)
        self.assertEqual(result["duplicate_sequences"], 1)
        self.assertEqual(result["out_of_order_sequences"], 1)
        self.assertEqual(result["sequence_errors"], 2)

    def test_proc_stat_ticks_handle_spaces_in_process_name(self) -> None:
        fields = ["S"] + ["0"] * 18
        fields[11] = "123"
        fields[12] = "45"
        self.assertEqual(parse_process_ticks("99 (bench process) " + " ".join(fields)), 168)

    def test_benchmark_pool_rule_is_bounded_and_records_queue_overlap(self) -> None:
        self.assertEqual([entry["chunk_count"] for entry in shm_pool_classes(8)], [32, 16, 10, 10, 10])
        self.assertEqual([entry["chunk_count"] for entry in shm_pool_classes(2)], [32, 16, 8, 4, 4])

    def test_summary_validation_and_repetition_aggregation(self) -> None:
        base = {
            "transport": "uds",
            "profile": "latency",
            "message_size_bytes": 64,
            "subscriber_count": 1,
            "messages_published": 10,
            "messages_received_total": 10,
            "payload_errors": 0,
            "sequence_errors": 0,
            "latency_sample_count": 10,
            "latency_p50_ns": 10.0,
            "latency_p90_ns": 20.0,
            "latency_p99_ns": 30.0,
            "messages_per_second": 100.0,
            "publisher_logical_MB_per_second": 1.0,
            "aggregate_delivered_MB_per_second": 1.0,
            "publisher_cpu_percent": 2.0,
            "subscriber_cpu_total_percent": 3.0,
            "publisher_rss_bytes": 1000,
            "subscriber_rss_total_bytes": 2000,
            "drop_count": 0,
            "drop_rate": 0.0,
            "queue_overflow_count": 0,
            "allocation_failure_count": 0,
            "blocked_count": 0,
            "blocked_time_ns": 0,
            "valid_result": True,
        }
        self.assertEqual(validate_summary(base), [])
        second = dict(base)
        second["latency_p50_ns"] = 14.0
        second["queue_overflow_count"] = 4
        second["blocked_count"] = 2
        aggregated = aggregate_repetitions([base, second])
        self.assertEqual(aggregated["latency_p50_ns"]["median"], 12.0)
        self.assertEqual(aggregated["queue_overflow_count"], {"median": 2.0, "min": 0.0, "max": 4.0})
        self.assertEqual(aggregated["blocked_count"], {"median": 1.0, "min": 0.0, "max": 2.0})
        self.assertEqual(aggregated["repetitions_valid"], 2)

    def test_full_config_expands_to_mandatory_432_repetitions(self) -> None:
        repo = pathlib.Path(__file__).resolve().parents[3]
        config = json.loads((repo / "benchmark/configs/full.json").read_text(encoding="utf-8"))
        args = types.SimpleNamespace(
            transport=None,
            skip_ros2=False,
            message_size=None,
            subscribers=None,
            profile=None,
            repetitions=None,
            skip_backpressure=True,
        )
        cases = build_cases(config, args)
        self.assertEqual(len(cases), 432)
        self.assertEqual({case.transport for case in cases}, {"uds", "shm_copy", "shm_loan", "ros2"})
        args.skip_backpressure = False
        all_cases = build_cases(config, args)
        self.assertEqual(len(all_cases), 441)
        self.assertEqual(sum(case.experiment == "backpressure" for case in all_cases), 9)


if __name__ == "__main__":
    unittest.main()
