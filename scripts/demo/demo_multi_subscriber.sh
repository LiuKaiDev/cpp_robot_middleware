#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

new_demo_case multi_subscriber
registry="/tmp/mw_demo_multi_registry_${DEMO_TOKEN}.sock"
topic="/demo/multi/${DEMO_TOKEN}"
subscriber_pids=()

for binary in mw_ping_publisher mw_ping_subscriber; do
    require_executable "${MW_BUILD_DIR}/bin/${binary}"
done

echo "Demo: one SHM publisher to four subscribers"
echo "transport=shm payload_size=65536 subscribers=4 topic=${topic}"
start_registry "${registry}"
for index in 0 1 2 3; do
    data_socket="/tmp/mw_demo_multi_${DEMO_TOKEN}_${index}.sock"
    register_cleanup_path "${data_socket}"
    start_background "subscriber_${index}" "${MW_BUILD_DIR}/bin/mw_ping_subscriber" \
        --registry "${registry}" --node-name "demo_multi_sub_${DEMO_TOKEN}_${index}" \
        --socket "${data_socket}" --topic "${topic}" --transport shm --count 1 --size 65536
    subscriber_pids+=("${DEMO_LAST_PID}")
    wait_for_path "${data_socket}" 10
done

run_bounded 20 "${MW_BUILD_DIR}/bin/mw_ping_publisher" \
    --registry "${registry}" --node-name "demo_multi_pub_${DEMO_TOKEN}" \
    --socket "/tmp/mw_demo_multi_pub_${DEMO_TOKEN}.sock" --topic "${topic}" \
    --transport shm --count 1 --size 65536 >"${MW_DEMO_CASE_DIR}/publisher.log" 2>&1

expected_identity=""
for index in 0 1 2 3; do
    wait_for_success "${subscriber_pids[index]}" 20
    print_log "subscriber_${index}"
    identity="$(sed -n 's/^.*\(pool_id=[0-9][0-9]* chunk_index=[0-9][0-9]* generation=[0-9][0-9]* payload_offset=[0-9][0-9]*\)$/\1/p' "${MW_DEMO_CASE_DIR}/subscriber_${index}.log")"
    if [[ -z "${identity}" ]]; then
        echo "subscriber ${index} did not report a logical chunk identity" >&2
        exit 1
    fi
    if [[ -z "${expected_identity}" ]]; then
        expected_identity="${identity}"
    elif [[ "${identity}" != "${expected_identity}" ]]; then
        echo "subscriber logical chunk identities differ" >&2
        exit 1
    fi
done
print_log publisher
echo "shared_logical_chunk=${expected_identity}"
echo "DEMO_RESULT multi_subscriber PASS"
