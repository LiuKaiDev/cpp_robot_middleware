#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

new_demo_case ros2_adapter
MW_ROS_INSTALL="${MW_ROS_INSTALL:-${MW_REPO_ROOT}/.work/public/ros2/install}"
MW_TO_ROS_EXECUTABLE="${MW_ROS_INSTALL}/mw_ros2_adapter/lib/mw_ros2_adapter/mw_to_ros2_bridge"
ROS_TO_MW_EXECUTABLE="${MW_ROS_INSTALL}/mw_ros2_adapter/lib/mw_ros2_adapter/ros2_to_mw_bridge"
registry="/tmp/mw_demo_ros_registry_${DEMO_TOKEN}.sock"
mw_socket="/tmp/mw_demo_ros_data_${DEMO_TOKEN}.sock"
mw_topic="/demo/ros/mw/${DEMO_TOKEN}"
ros_input="/demo/ros/input/${DEMO_TOKEN}"
ros_output="/demo/ros/output/${DEMO_TOKEN}"
payload="public-demo-${DEMO_TOKEN}"
register_cleanup_path "${mw_socket}"

if [[ ! -f /opt/ros/jazzy/setup.bash || ! -f "${MW_ROS_INSTALL}/setup.bash" ]]; then
    echo "ROS2 Jazzy or the public adapter install is unavailable" >&2
    exit 2
fi
require_executable "${MW_TO_ROS_EXECUTABLE}"
require_executable "${ROS_TO_MW_EXECUTABLE}"
set +u
source /opt/ros/jazzy/setup.bash
source "${MW_ROS_INSTALL}/setup.bash"
set -u
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
export ROS_DOMAIN_ID=$((200 + BASHPID % 20))

echo "Demo: ROS2 String -> middleware SHM -> ROS2 String"
echo "ros_input=${ros_input} mw_topic=${mw_topic} ros_output=${ros_output}"
start_registry "${registry}"

start_background mw_to_ros "${MW_TO_ROS_EXECUTABLE}" --ros-args \
    -p "registry_socket:=${registry}" -p "ros_topic:=${ros_output}" \
    -p "mw_topic:=${mw_topic}" -p "mw_socket_path:=${mw_socket}" \
    -p message_type:=std_msgs/msg/String -p transport:=shm
wait_for_path "${mw_socket}" 15
start_background ros_to_mw "${ROS_TO_MW_EXECUTABLE}" --ros-args \
    -p "registry_socket:=${registry}" -p "ros_topic:=${ros_input}" \
    -p "mw_topic:=${mw_topic}" -p message_type:=std_msgs/msg/String -p transport:=shm

ready=false
for _ in {1..200}; do
    if "${MW_BUILD_DIR}/bin/mwctl" --registry "${registry}" topic info "${mw_topic}" \
        >"${MW_DEMO_CASE_DIR}/topic_info.log" 2>&1; then
        if rg -q 'publishers: 1' "${MW_DEMO_CASE_DIR}/topic_info.log" && \
           rg -q 'subscribers: 1' "${MW_DEMO_CASE_DIR}/topic_info.log"; then
            ready=true
            break
        fi
    fi
    sleep 0.05
done
if [[ "${ready}" != true ]]; then
    echo "bridge pair did not become ready" >&2
    exit 1
fi

start_background ros_echo timeout --signal=TERM --kill-after=3s 30s \
    ros2 topic echo --once "${ros_output}" std_msgs/msg/String
echo_pid="${DEMO_LAST_PID}"
echo_ready=false
for _ in {1..200}; do
    if ros2 topic info "${ros_output}" >"${MW_DEMO_CASE_DIR}/ros_output_info.log" 2>&1 && \
       rg -q 'Subscription count: [1-9][0-9]*' \
           "${MW_DEMO_CASE_DIR}/ros_output_info.log"; then
        echo_ready=true
        break
    fi
    if ! kill -0 "${echo_pid}" 2>/dev/null; then
        break
    fi
    sleep 0.05
done
if [[ "${echo_ready}" != true ]]; then
    echo "ROS2 output subscriber did not become ready" >&2
    exit 1
fi
run_bounded 30 ros2 topic pub --once "${ros_input}" std_msgs/msg/String \
    "{data: '${payload}'}" >"${MW_DEMO_CASE_DIR}/ros_pub.log" 2>&1
wait_for_success "${echo_pid}" 30

print_log topic_info
print_log ros_pub
print_log ros_echo
rg -q "${payload}" "${MW_DEMO_CASE_DIR}/ros_echo.log"
echo "DEMO_RESULT ros2_adapter PASS"
