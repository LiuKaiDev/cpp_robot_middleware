#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

new_demo_case basic
registry="/tmp/mw_demo_basic_registry_${DEMO_TOKEN}.sock"
data_socket="/tmp/mw_demo_basic_data_${DEMO_TOKEN}.sock"
topic="/demo/basic/${DEMO_TOKEN}"
register_cleanup_path "${data_socket}"

for binary in mw_ping_publisher mw_ping_subscriber mwctl; do
    require_executable "${MW_BUILD_DIR}/bin/${binary}"
done

echo "Demo: basic publish/subscribe"
echo "transport=shm payload_size=64 messages=5 topic=${topic}"
start_registry "${registry}"
start_background subscriber "${MW_BUILD_DIR}/bin/mw_ping_subscriber" \
    --registry "${registry}" --node-name "demo_basic_sub_${DEMO_TOKEN}" \
    --socket "${data_socket}" --topic "${topic}" --transport shm --count 5 --size 64
subscriber_pid="${DEMO_LAST_PID}"
wait_for_path "${data_socket}" 10

"${MW_BUILD_DIR}/bin/mwctl" --registry "${registry}" node list
"${MW_BUILD_DIR}/bin/mwctl" --registry "${registry}" topic info "${topic}"
run_bounded 20 "${MW_BUILD_DIR}/bin/mw_ping_publisher" \
    --registry "${registry}" --node-name "demo_basic_pub_${DEMO_TOKEN}" \
    --socket "/tmp/mw_demo_basic_pub_${DEMO_TOKEN}.sock" --topic "${topic}" \
    --transport shm --count 5 --size 64 >"${MW_DEMO_CASE_DIR}/publisher.log" 2>&1
wait_for_success "${subscriber_pid}" 20

print_log publisher
print_log subscriber
"${MW_BUILD_DIR}/bin/mwctl" --registry "${registry}" stats
rg -q 'sent=5 publish_errors=0' "${MW_DEMO_CASE_DIR}/publisher.log"
rg -q 'received=5 sequence_errors=0 payload_errors=0' "${MW_DEMO_CASE_DIR}/subscriber.log"
echo "DEMO_RESULT basic_pubsub PASS"
