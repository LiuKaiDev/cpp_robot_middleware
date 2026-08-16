#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

new_demo_case crash_recovery
registry="/tmp/mw_demo_crash_registry_${DEMO_TOKEN}.sock"
data_socket="/tmp/mw_demo_crash_data_${DEMO_TOKEN}.sock"
topic="/demo/crash/${DEMO_TOKEN}"
node_name="demo_crash_sub_${DEMO_TOKEN}"
register_cleanup_path "${data_socket}"

for binary in mw_ping_publisher mw_ping_subscriber mwctl; do
    require_executable "${MW_BUILD_DIR}/bin/${binary}"
done

echo "Demo: subscriber SIGKILL and replacement"
start_registry "${registry}"
start_background crashed_subscriber "${MW_BUILD_DIR}/bin/mw_ping_subscriber" \
    --registry "${registry}" --node-name "${node_name}" --socket "${data_socket}" \
    --topic "${topic}" --transport shm --count 100 --size 64 --timeout-ms 1000
crashed_pid="${DEMO_LAST_PID}"
wait_for_path "${data_socket}" 10
echo "before_sigkill:"
"${MW_BUILD_DIR}/bin/mwctl" --registry "${registry}" node list

kill -KILL "${crashed_pid}"
crash_status=0
wait "${crashed_pid}" 2>/dev/null || crash_status=$?
echo "killed_pid=${crashed_pid} exit_status=${crash_status}"

removed=false
for _ in {1..100}; do
    node_output="$("${MW_BUILD_DIR}/bin/mwctl" --registry "${registry}" node list)"
    if ! rg -q "${node_name}" <<<"${node_output}"; then
        removed=true
        break
    fi
    sleep 0.05
done
if [[ "${removed}" != true ]]; then
    echo "dead subscriber remained in registry" >&2
    exit 1
fi
echo "after_sigkill:"
"${MW_BUILD_DIR}/bin/mwctl" --registry "${registry}" node list

start_background replacement_subscriber "${MW_BUILD_DIR}/bin/mw_ping_subscriber" \
    --registry "${registry}" --node-name "${node_name}" --socket "${data_socket}" \
    --topic "${topic}" --transport shm --count 1 --size 64
replacement_pid="${DEMO_LAST_PID}"
wait_for_path "${data_socket}" 10
run_bounded 20 "${MW_BUILD_DIR}/bin/mw_ping_publisher" \
    --registry "${registry}" --node-name "demo_crash_pub_${DEMO_TOKEN}" \
    --socket "/tmp/mw_demo_crash_pub_${DEMO_TOKEN}.sock" --topic "${topic}" \
    --transport shm --count 1 --size 64 >"${MW_DEMO_CASE_DIR}/publisher.log" 2>&1
wait_for_success "${replacement_pid}" 20

print_log publisher
print_log replacement_subscriber
rg -q 'received=1 sequence_errors=0 payload_errors=0' \
    "${MW_DEMO_CASE_DIR}/replacement_subscriber.log"
echo "DEMO_RESULT crash_recovery PASS"
