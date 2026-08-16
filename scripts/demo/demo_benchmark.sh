#!/usr/bin/env bash

set -euo pipefail
script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

python3 "${script_dir}/benchmark_summary.py"
echo "charts:"
echo "  ${repo_root}/benchmark/results/reference_current/latency_vs_message_size.png"
echo "  ${repo_root}/benchmark/results/reference_current/throughput_vs_message_size.png"
echo "  ${repo_root}/benchmark/results/reference_current/cpu_vs_message_size.png"
echo "  ${repo_root}/benchmark/results/reference_current/subscriber_count_vs_throughput.png"
echo "DEMO_RESULT benchmark PASS"
