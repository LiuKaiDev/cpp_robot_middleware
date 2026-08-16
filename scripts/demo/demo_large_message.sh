#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

new_demo_case large_message
registry="/tmp/mw_demo_large_registry_${DEMO_TOKEN}.sock"
data_socket="/tmp/mw_demo_large_data_${DEMO_TOKEN}.sock"
topic="/demo/large/${DEMO_TOKEN}"
register_cleanup_path "${data_socket}"

for binary in mw_ping_publisher mw_ping_subscriber; do
    require_executable "${MW_BUILD_DIR}/bin/${binary}"
done

echo "Demo: 4 MiB SHM Copy"
echo "transport=shm payload_size=4194304 messages=3 topic=${topic}"
start_registry "${registry}"
start_background subscriber "${MW_BUILD_DIR}/bin/mw_ping_subscriber" \
    --registry "${registry}" --node-name "demo_large_sub_${DEMO_TOKEN}" \
    --socket "${data_socket}" --topic "${topic}" --transport shm --count 3 --size 4194304 \
    --timeout-ms 10000
subscriber_pid="${DEMO_LAST_PID}"
wait_for_path "${data_socket}" 10

run_bounded 30 "${MW_BUILD_DIR}/bin/mw_ping_publisher" \
    --registry "${registry}" --node-name "demo_large_pub_${DEMO_TOKEN}" \
    --socket "/tmp/mw_demo_large_pub_${DEMO_TOKEN}.sock" --topic "${topic}" \
    --transport shm --count 3 --size 4194304 >"${MW_DEMO_CASE_DIR}/publisher.log" 2>&1
wait_for_success "${subscriber_pid}" 30

print_log publisher
print_log subscriber
rg -q 'sent=3 publish_errors=0' "${MW_DEMO_CASE_DIR}/publisher.log"
rg -q 'received=3 sequence_errors=0 payload_errors=0' "${MW_DEMO_CASE_DIR}/subscriber.log"
echo "DEMO_RESULT large_message PASS"
