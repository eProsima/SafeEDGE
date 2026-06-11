#!/usr/bin/env bash
# Launch the full SafeEDGE stack and verify DDS connectivity.
#
# Order:
#   1. Stop any previous instances.
#   2. Build images/binaries if needed.
#   3. Start hypervisor (safety + non-safety guests).
#   4. Once guests are SSH-reachable, start edge and server Docker containers.
#   5. Wait for DDS connectivity and print a final verdict.
#
# Usage: bash scripts/launch_all.sh [--no-rebuild] [-h|--help]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

HV_LOG="/tmp/safe-edge-hypervisor.log"
EDGE_LOG="/tmp/safe-edge-edge.log"
SERVER_LOG="/tmp/safe-edge-server.log"

SAFETY_CTL="/tmp/ssh-guest-safety-ctl"
NON_SAFETY_CTL="/tmp/ssh-guest-non-safety-ctl"
SAFETY_IP="192.168.10.2"
NON_SAFETY_IP="192.168.20.2"

VERIFY_TIMEOUT=60   # seconds to wait for DDS connectivity
OPT_NO_REBUILD=0
OPT_STOP=0

log()  { echo "$*"; }
ok()   { echo "✓ $*"; }
fail() { echo "✗ $*" >&2; }

_stop_all() {
    for cname in safe-edge-edge safe-edge-server; do
        if docker ps -q --filter name="^${cname}$" | grep -q .; then
            log "Stopping ${cname}..."
            docker stop "${cname}" > /dev/null 2>&1 || true
            docker rm   "${cname}" > /dev/null 2>&1 || true
            ok "${cname} stopped."
        fi
    done
    bash "${SCRIPT_DIR}/launch_hypervisor_split.sh" --stop 2>/dev/null || true
    sleep 2
    pkill -f qemu-system 2>/dev/null || true
}

usage() {
    cat <<EOF
Usage: bash launch_all.sh [--no-rebuild] [--stop] [-h|--help]

Launches the complete SafeEDGE stack (hypervisor + edge + server) and
verifies that DDS connectivity is established across all domains.

Options:
  --no-rebuild   Skip image/binary rebuilds (use existing artifacts).
  --stop         Stop all running instances and exit.
  -h|--help      Show this help.
EOF
}

for arg in "$@"; do
    case "${arg}" in
        --no-rebuild) OPT_NO_REBUILD=1 ;;
        --stop)       OPT_STOP=1 ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "Unknown option: ${arg}" >&2; usage >&2; exit 1 ;;
    esac
done

if [[ "${OPT_STOP}" -eq 1 ]]; then
    _stop_all
    ok "All stopped."
    exit 0
fi

ssh_guest() {
    local ctl="${1}" ip="${2}"; shift 2
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o ControlPath="${ctl}" -o ConnectTimeout=5 \
        root@"${ip}" "$@" 2>/dev/null
}

# ── 1. Stop previous instances ────────────────────────────────────────────────

log "Stopping any previous SafeEDGE instances..."
_stop_all
sleep 2

# ── 2. Build ──────────────────────────────────────────────────────────────────

if [[ "${OPT_NO_REBUILD}" -eq 0 ]]; then
    log "Building Docker images..."
    bash "${SCRIPT_DIR}/build_ubuntu.sh"
    log "Building QNX binaries..."
    bash "${SCRIPT_DIR}/build_qnx.sh"
fi

# ── Topology ─────────────────────────────────────────────────────────────────

BRIDGE_IP="$(ip -o -f inet addr show virbr0 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)"
: "${BRIDGE_IP:=127.0.0.1}"
export SAFE_EDGE_OWN_IP="${BRIDGE_IP}"
export SAFE_EDGE_SAFETY_IP="${SAFETY_IP}"
export SAFE_EDGE_NON_SAFETY_IP="${NON_SAFETY_IP}"
export SAFE_EDGE_INITIAL_PEERS_EDGE="${SAFETY_IP}:8001,${SAFETY_IP}:8002,${NON_SAFETY_IP}:8011,${BRIDGE_IP}:8020"
export SAFE_EDGE_INITIAL_PEERS_SERVER="${NON_SAFETY_IP}:8011,${BRIDGE_IP}:8030"
log "Bridge IP: ${BRIDGE_IP}  Safety: ${SAFETY_IP}  Non-safety: ${NON_SAFETY_IP}"

# ── 3. Start hypervisor ───────────────────────────────────────────────────────

log "Starting hypervisor (safety + non-safety guests)..."
HV_ARGS=()
[[ "${OPT_NO_REBUILD}" -eq 1 ]] && HV_ARGS+=(--no-rebuild)
bash "${SCRIPT_DIR}/launch_hypervisor_split.sh" "${HV_ARGS[@]}" > "${HV_LOG}" 2>&1 &
HV_PID=$!

log "Waiting for guests to be ready..."
DEADLINE=$(( $(date +%s) + 300 ))
while ! grep -q "Both guests are running" "${HV_LOG}" 2>/dev/null; do
    if [[ $(date +%s) -gt ${DEADLINE} ]]; then
        fail "Timed out waiting for hypervisor guests."
        tail -20 "${HV_LOG}" >&2
        exit 1
    fi
    sleep 3
done
ok "Guests are up."

# ── 4. Start edge and server ──────────────────────────────────────────────────

log "Starting edge and server..."
SAFE_EDGE_INITIAL_PEERS="${SAFE_EDGE_INITIAL_PEERS_EDGE}"   bash "${SCRIPT_DIR}/launch_fast_edge_test.sh"   > "${EDGE_LOG}"   2>&1 &
SAFE_EDGE_INITIAL_PEERS="${SAFE_EDGE_INITIAL_PEERS_SERVER}" bash "${SCRIPT_DIR}/launch_fast_server_test.sh" > "${SERVER_LOG}" 2>&1 &

# Wait for both named containers to be running.
DEADLINE=$(( $(date +%s) + 30 ))
while [[ $(docker ps -q --filter name=^safe-edge-edge$   | wc -l) -eq 0 ]] || \
      [[ $(docker ps -q --filter name=^safe-edge-server$ | wc -l) -eq 0 ]]; do
    if [[ $(date +%s) -gt ${DEADLINE} ]]; then
        fail "Timed out waiting for Docker containers."
        exit 1
    fi
    sleep 2
done
ok "Edge and server containers running (safe-edge-edge, safe-edge-server)."

# ── 5. Verify DDS connectivity ────────────────────────────────────────────────
#
# Checks in logical order:
#   [1] safety ↔ non-safety   (guest-guest, within hypervisor)
#   [2] safety ↔ edge         (safety domain → Docker)
#   [3] edge   ↔ server       (Docker ↔ Docker)
#   [4] non-safety ↔ server   (non-safety domain → Docker)

log "Waiting for DDS connectivity (up to ${VERIFY_TIMEOUT}s)..."
GUEST_GUEST_OK=0
SAFETY_EDGE_OK=0
EDGE_SERVER_OK=0
NS_SERVER_OK=0
DEADLINE=$(( $(date +%s) + VERIFY_TIMEOUT ))

_edge_id() { docker ps -q --filter name=^safe-edge-edge$ 2>/dev/null | head -1; }

while [[ $(date +%s) -le ${DEADLINE} ]]; do

    # [1] safety ↔ non-safety
    # Signal: cloud_gateway published ServerAvailabilityStatus to policy_engine reader
    #         → "Publication matched on safe_edge.internal.server_availability_status"
    if [[ "${GUEST_GUEST_OK}" -eq 0 ]]; then
        if ssh_guest "${NON_SAFETY_CTL}" "${NON_SAFETY_IP}" \
                "grep -q 'Publication matched on safe_edge.internal.server_availability_status' \
                 /data/safe-edge-non-safety/logs/safe_edge_cloud_gateway.log 2>/dev/null"; then
            GUEST_GUEST_OK=1
            ok "[1] safety ↔ non-safety: connected  (cloud_gateway→ServerAvailabilityStatus matched policy_engine reader)"
        fi
    fi

    # [2] safety ↔ edge
    # Signal: edge reader matched safety writer on vehicle_edge_summary
    #         → "Subscription matched on safe_edge.edge.vehicle_edge_summary"
    if [[ "${SAFETY_EDGE_OK}" -eq 0 ]]; then
        local_edge_id="$(_edge_id)"
        if [[ -n "${local_edge_id}" ]]; then
            if docker logs "${local_edge_id}" 2>/dev/null \
                    | grep -q "Subscription matched on safe_edge.edge.vehicle_edge_summary"; then
                SAFETY_EDGE_OK=1
                ok "[2] safety ↔ edge:         connected  (edge subscribed to VehicleEdgeSummary from safety)"
            fi
        fi
    fi

    # [3] edge ↔ server
    # Signal: edge publishes EdgeGatewayStatus=OK (server heartbeat received, charger_locations matched)
    if [[ "${EDGE_SERVER_OK}" -eq 0 ]]; then
        local_edge_id="$(_edge_id)"
        if [[ -n "${local_edge_id}" ]]; then
            if docker logs "${local_edge_id}" 2>/dev/null \
                    | grep -q "EdgeGatewayStatus status=OK"; then
                EDGE_SERVER_OK=1
                ok "[3] edge   ↔ server:       connected  (EdgeGatewayStatus=OK)"
            fi
        fi
    fi

    # [4] non-safety ↔ server
    # Signal: cloud_gateway received charger_locations from server and published ServerAvailabilityStatus=true
    if [[ "${NS_SERVER_OK}" -eq 0 ]]; then
        if ssh_guest "${NON_SAFETY_CTL}" "${NON_SAFETY_IP}" \
                "grep -q 'server_available=true' \
                 /data/safe-edge-non-safety/logs/safe_edge_cloud_gateway.log 2>/dev/null"; then
            NS_SERVER_OK=1
            ok "[4] non-safety ↔ server:   connected  (ServerAvailabilityStatus server_available=true)"
        fi
    fi

    [[ "${GUEST_GUEST_OK}" -eq 1 && "${SAFETY_EDGE_OK}" -eq 1 && \
       "${EDGE_SERVER_OK}" -eq 1 && "${NS_SERVER_OK}"  -eq 1 ]] && break
    sleep 3
done

# ── Result ───────────────────────────────────────────────────────────────────

echo ""
echo "──────────────────────────────────────────────────────"
ALL_OK=0
[[ "${GUEST_GUEST_OK}" -eq 1 && "${SAFETY_EDGE_OK}" -eq 1 && \
   "${EDGE_SERVER_OK}" -eq 1 && "${NS_SERVER_OK}"   -eq 1 ]] && ALL_OK=1

if [[ "${ALL_OK}" -eq 1 ]]; then
    echo "ALL SYSTEMS CONNECTED"
else
    [[ "${GUEST_GUEST_OK}" -eq 0 ]] && fail "[1] safety ↔ non-safety:  NOT connected"
    [[ "${SAFETY_EDGE_OK}" -eq 0 ]] && fail "[2] safety ↔ edge:        NOT connected"
    [[ "${EDGE_SERVER_OK}" -eq 0 ]] && fail "[3] edge   ↔ server:      NOT connected"
    [[ "${NS_SERVER_OK}"   -eq 0 ]] && fail "[4] non-safety ↔ server:  NOT connected"
fi
echo "──────────────────────────────────────────────────────"
echo ""
echo "Logs:"
echo "  Hypervisor : ${HV_LOG}"
echo "  Edge       : ${EDGE_LOG}"
echo "  Server     : ${SERVER_LOG}"
echo "  Safety     : ssh -o ControlPath=${SAFETY_CTL} root@${SAFETY_IP}"
echo "  Non-safety : ssh -o ControlPath=${NON_SAFETY_CTL} root@${NON_SAFETY_IP}"
[[ "${ALL_OK}" -eq 0 ]] && exit 1
echo ""
echo "Stop with: bash scripts/launch_all.sh --stop"
