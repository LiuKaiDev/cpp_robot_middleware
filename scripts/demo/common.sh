#!/usr/bin/env bash

set -euo pipefail

DEMO_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MW_REPO_ROOT="$(cd "${DEMO_SCRIPT_DIR}/../.." && pwd)"
MW_BUILD_DIR="${MW_BUILD_DIR:-${MW_REPO_ROOT}/.work/phase_9/build_release}"
MW_DEMO_ROOT="${MW_DEMO_ROOT:-${MW_REPO_ROOT}/.work/phase_9/demo}"
DEMO_TOKEN="t${BASHPID}_${RANDOM}"
DEMO_PIDS=()
DEMO_PATHS=()
DEMO_LAST_PID=""
MW_DEMO_CASE_DIR=""

cleanup_demo() {
    local index pid path
    for ((index=${#DEMO_PIDS[@]} - 1; index >= 0; --index)); do
        pid="${DEMO_PIDS[index]}"
        if kill -0 "${pid}" 2>/dev/null; then
            kill -TERM "${pid}" 2>/dev/null || true
        fi
    done
    for ((index=${#DEMO_PIDS[@]} - 1; index >= 0; --index)); do
        pid="${DEMO_PIDS[index]}"
        if kill -0 "${pid}" 2>/dev/null; then
            for _ in {1..30}; do
                kill -0 "${pid}" 2>/dev/null || break
                sleep 0.05
            done
        fi
        if kill -0 "${pid}" 2>/dev/null; then
            kill -KILL "${pid}" 2>/dev/null || true
        fi
        wait "${pid}" 2>/dev/null || true
    done
    for path in "${DEMO_PATHS[@]}"; do
        rm -f -- "${path}"
    done
}

trap cleanup_demo EXIT

new_demo_case() {
    local name="$1"
    MW_DEMO_CASE_DIR="${MW_DEMO_ROOT}/${name}_${DEMO_TOKEN}"
    mkdir -p "${MW_DEMO_CASE_DIR}"
}

register_cleanup_path() {
    DEMO_PATHS+=("$1")
}

require_executable() {
    if [[ ! -x "$1" ]]; then
        echo "missing executable: $1" >&2
        exit 2
    fi
}

start_background() {
    local name="$1"
    shift
    "$@" >"${MW_DEMO_CASE_DIR}/${name}.log" 2>&1 &
    DEMO_LAST_PID=$!
    DEMO_PIDS+=("${DEMO_LAST_PID}")
}

wait_for_path() {
    local path="$1"
    local timeout_seconds="$2"
    local deadline=$((SECONDS + timeout_seconds))
    while [[ ! -e "${path}" ]]; do
        if ((SECONDS >= deadline)); then
            echo "timed out waiting for ${path}" >&2
            return 1
        fi
        sleep 0.05
    done
}

wait_for_exit() {
    local pid="$1"
    local timeout_seconds="$2"
    local deadline=$((SECONDS + timeout_seconds))
    while kill -0 "${pid}" 2>/dev/null; do
        if ((SECONDS >= deadline)); then
            echo "process ${pid} did not exit within ${timeout_seconds}s" >&2
            return 1
        fi
        sleep 0.05
    done
}

wait_for_success() {
    local pid="$1"
    local timeout_seconds="$2"
    local status=0
    wait_for_exit "${pid}" "${timeout_seconds}"
    wait "${pid}" || status=$?
    if ((status != 0)); then
        echo "process ${pid} exited with status ${status}" >&2
        return "${status}"
    fi
}

run_bounded() {
    local timeout_seconds="$1"
    shift
    timeout --signal=TERM --kill-after=3s "${timeout_seconds}s" "$@"
}

print_log() {
    local name="$1"
    echo "--- ${name} ---"
    sed -n '1,160p' "${MW_DEMO_CASE_DIR}/${name}.log"
}

start_registry() {
    local registry_path="$1"
    require_executable "${MW_BUILD_DIR}/bin/mw_registryd"
    register_cleanup_path "${registry_path}"
    start_background registry "${MW_BUILD_DIR}/bin/mw_registryd" --socket "${registry_path}"
    wait_for_path "${registry_path}" 10
}
