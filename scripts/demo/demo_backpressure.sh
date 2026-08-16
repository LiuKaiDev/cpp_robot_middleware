#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

new_demo_case backpressure
require_executable "${MW_BUILD_DIR}/bin/mw_bench_publisher"
require_executable "${MW_BUILD_DIR}/bin/mw_bench_subscriber"

echo "Demo: bounded queue backpressure policies"
run_bounded 90 python3 "${MW_REPO_ROOT}/benchmark/python/run_benchmarks.py" \
    --config "${DEMO_SCRIPT_DIR}/backpressure_smoke.json" \
    --results-root "${MW_DEMO_CASE_DIR}/results" \
    --build-dir "${MW_BUILD_DIR}" \
    --run-id backpressure_demo --skip-ros2 --no-plots \
    >"${MW_DEMO_CASE_DIR}/runner.log" 2>&1

print_log runner
python3 "${DEMO_SCRIPT_DIR}/benchmark_summary.py" \
    --reference "${MW_DEMO_CASE_DIR}/results/backpressure_demo/aggregated/summary.json" \
    --backpressure-only
echo "DEMO_RESULT backpressure PASS"
