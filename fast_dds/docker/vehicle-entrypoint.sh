#!/usr/bin/env bash
set -euo pipefail

LOG_DIR=/var/log/safe-edge
INPUT_DIR=/data/safe-edge-stage2
INPUT_FILE="${SAFE_EDGE_INPUT_FILE:-${INPUT_DIR}/input.txt}"

mkdir -p "${LOG_DIR}" "${INPUT_DIR}"

cat > "${INPUT_FILE}" <<'EOF'
soc=50.0
emergency_stop=0
adas_fault=0
available_charge_kw=50.0
available_discharge_kw=50.0
v2g_ready=1
speed_mps=0.0
braking_available=1
steering_available=1
EOF

PIDS=()
TAIL_PID=""

start_node() {
    local name="$1"
    shift
    "$@" > "${LOG_DIR}/${name}.log" 2>&1 &
    PIDS+=($!)
}

stop_all() {
    local pid
    if [[ -n "${TAIL_PID}" ]]; then
        kill "${TAIL_PID}" 2>/dev/null || true
    fi
    for pid in "${PIDS[@]+"${PIDS[@]}"}"; do
        kill "${pid}" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}

trap stop_all EXIT INT TERM

start_node safe_edge_infotainment /opt/safe-edge/non-safety/bin/safe_edge_infotainment
sleep 1
start_node safe_edge_cloud_gateway /opt/safe-edge/non-safety/bin/safe_edge_cloud_gateway
start_node safe_edge_ota_service /opt/safe-edge/non-safety/bin/safe_edge_ota_service
start_node safe_edge_safety_io_adapters /opt/safe-edge/safety/bin/safe_edge_safety_io_adapters
start_node safe_edge_policy_engine /opt/safe-edge/safety/bin/safe_edge_policy_engine
start_node safe_edge_vehicle_mock /opt/safe-edge/safety/bin/safe_edge_vehicle_mock

tail -n +1 -F "${LOG_DIR}"/*.log &
TAIL_PID=$!

wait -n "${PIDS[@]}"
