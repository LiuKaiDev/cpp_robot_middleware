#!/usr/bin/env python3
"""Validate per-run summaries and aggregate repetitions without changing raw data."""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
from collections import defaultdict
from typing import Any

from benchmark_analysis import aggregate_repetitions, validate_summary


def load_summaries(run_root: pathlib.Path) -> list[dict[str, Any]]:
    summaries: list[dict[str, Any]] = []
    for path in sorted(run_root.rglob("summary.json")):
        if "aggregated" in path.parts:
            continue
        summary = json.loads(path.read_text(encoding="utf-8"))
        issues = validate_summary(summary)
        if issues:
            raise ValueError(f"{path}: {'; '.join(issues)}")
        summary["run_path"] = str(path.parent.relative_to(run_root))
        summaries.append(summary)
    if not summaries:
        raise ValueError(f"no per-run summary.json files found below {run_root}")
    return summaries


def aggregate(run_root: pathlib.Path) -> dict[str, Any]:
    summaries = load_summaries(run_root)
    groups: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    for summary in summaries:
        key = (
            summary["transport"],
            summary["profile"],
            summary["message_size_bytes"],
            summary["subscriber_count"],
            summary.get("experiment", "main"),
            summary.get("overflow_policy", ""),
        )
        groups[key].append(summary)
    cases = []
    for key in sorted(groups, key=lambda item: (item[2], item[3], item[1], item[0], item[4], item[5])):
        case = aggregate_repetitions(groups[key])
        case["experiment"] = key[4]
        case["overflow_policy"] = key[5]
        cases.append(case)
    return {
        "schema_version": 1,
        "run_root": str(run_root),
        "run_count": len(summaries),
        "valid_run_count": sum(bool(summary["valid_result"]) for summary in summaries),
        "all_valid": all(bool(summary["valid_result"]) for summary in summaries),
        "cases": cases,
    }


def write_outputs(run_root: pathlib.Path, result: dict[str, Any]) -> None:
    output_dir = run_root / "aggregated"
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    rows = []
    for case in result["cases"]:
        row = {
            "transport": case["transport"],
            "profile": case["profile"],
            "message_size_bytes": case["message_size_bytes"],
            "subscriber_count": case["subscriber_count"],
            "experiment": case["experiment"],
            "overflow_policy": case["overflow_policy"],
            "repetitions_total": case["repetitions_total"],
            "repetitions_valid": case["repetitions_valid"],
            "valid_result": case["valid_result"],
        }
        for metric, values in case.items():
            if isinstance(values, dict) and {"median", "min", "max"} <= values.keys():
                for suffix in ("median", "min", "max"):
                    row[f"{metric}_{suffix}"] = values[suffix]
        rows.append(row)
    fieldnames = sorted({field for row in rows for field in row})
    with (output_dir / "summary.csv").open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run_root", type=pathlib.Path)
    args = parser.parse_args()
    result = aggregate(args.run_root.resolve())
    write_outputs(args.run_root.resolve(), result)
    print(json.dumps({"runs": result["run_count"], "all_valid": result["all_valid"]}))
    return 0 if result["all_valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
