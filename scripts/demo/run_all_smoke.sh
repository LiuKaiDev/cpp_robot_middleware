#!/usr/bin/env bash

set -euo pipefail
script_dir="$(cd "$(dirname "$0")" && pwd)"
with_ros2=false
if [[ "${1:-}" == "--with-ros2" ]]; then
    with_ros2=true
elif [[ $# -ne 0 ]]; then
    echo "usage: run_all_smoke.sh [--with-ros2]" >&2
    exit 2
fi

for demo in basic_pubsub large_message multi_subscriber backpressure crash_recovery benchmark; do
    echo "=== ${demo} ==="
    "${script_dir}/demo_${demo}.sh"
done

if [[ "${with_ros2}" == true ]]; then
    echo "=== ros2_adapter ==="
    "${script_dir}/demo_ros2_adapter.sh"
fi
echo "DEMO_SMOKE_RESULT PASS"
