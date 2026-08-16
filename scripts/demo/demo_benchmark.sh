#!/usr/bin/env bash

set -euo pipefail
script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

python3 "${script_dir}/benchmark_summary.py"
echo "charts:"
echo "  ${repo_root}/benchmark/results/phase8_1_reference/latency_vs_message_size.png"
echo "  ${repo_root}/benchmark/results/phase8_1_reference/throughput_vs_message_size.png"
echo "  ${repo_root}/benchmark/results/phase8_1_reference/cpu_vs_message_size.png"
echo "  ${repo_root}/benchmark/results/phase8_1_reference/subscriber_count_vs_throughput.png"
echo "DEMO_RESULT benchmark PASS"
