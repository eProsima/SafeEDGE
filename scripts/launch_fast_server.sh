#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

LOG_DIR="${SCRIPT_DIR}/logs"
OPT_TEST=0

usage() {
    cat <<EOF
Usage: bash launch_fast_server.sh [--test] [-h|--help]

Without --test  Runs safe_edge_server as a persistent DDS service.
                Image: safe-edge-server:fast
                Env vars (optional):
                  SAFE_EDGE_OWN_IP            IP this node announces (default: 127.0.0.1)
                  SAFE_EDGE_CROSS_DOMAIN_IP   IP of the cross-domain peer (default: 127.0.0.1)
                  SAFE_EDGE_NON_SAFETY_IP     IP of the non-safety domain guest (default: 127.0.0.1)

--test          Runs the FastDDS server integration test (self-contained, loopback only).
                Image: safe-edge-server:fast-test
                Log: scripts/logs/launch_fast_server.log
EOF
}

for arg in "$@"; do
    case "${arg}" in
        --test)    OPT_TEST=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: ${arg}" >&2; usage >&2; exit 1 ;;
    esac
done

# Remove stale FastDDS shared-memory artifacts.
rm -f /dev/shm/fastdds_* /dev/shm/sem.fastdds_* 2>/dev/null || true

if [[ "${OPT_TEST}" -eq 1 ]]; then
    IMAGE="safe-edge-server:fast-test"
    LOG_FILE="${LOG_DIR}/launch_fast_server.log"
    DOCKER_ARGS=(--network host)
    mkdir -p "${LOG_DIR}"

    docker rm -f safe-edge-server-test 2>/dev/null || true

    if ! docker image inspect "${IMAGE}" > /dev/null 2>&1; then
        echo "[server] Image ${IMAGE} not found — building..."
        bash "${SCRIPT_DIR}/build_ubuntu.sh" --tests
    fi

    if [[ -z "${PILOT_API_KEY:-}" && -f /etc/safe-edge/server.ini ]]; then
        PILOT_API_KEY="$(grep -m1 'api_key' /etc/safe-edge/server.ini | cut -d'=' -f2 | tr -d ' ')"
    fi
    [[ -n "${PILOT_API_KEY:-}" ]] && DOCKER_ARGS+=(-e "PILOT_API_KEY=${PILOT_API_KEY}")

    echo "[server] Running integration test..."
    echo "[server] Log: ${LOG_FILE}"

    set +e
    docker run --name safe-edge-server-test "${DOCKER_ARGS[@]}" "${IMAGE}" 2>&1 | tee "${LOG_FILE}"
    TEST_EXIT=${PIPESTATUS[0]}
    set -e

    docker rm -f safe-edge-server-test 2>/dev/null || true

    if [[ ${TEST_EXIT} -eq 0 ]]; then
        echo "[server] PASSED"
    else
        echo "[server] FAILED (exit ${TEST_EXIT})" >&2
    fi
    exit "${TEST_EXIT}"
fi

# Service mode
IMAGE="safe-edge-server:fast"

if ! docker image inspect "${IMAGE}" > /dev/null 2>&1; then
    echo "[server] Image ${IMAGE} not found — building..."
    bash "${SCRIPT_DIR}/build_ubuntu.sh"
fi

# Auto-detect virbr0 IP (Linux↔QEMU bridge) when not overridden.
: "${SAFE_EDGE_OWN_IP:=$(ip -o -f inet addr show virbr0 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)}"
: "${SAFE_EDGE_OWN_IP:=127.0.0.1}"
# Non-safety guest IP is fixed by the hypervisor split configuration.
: "${SAFE_EDGE_NON_SAFETY_IP:=192.168.20.2}"
# Build explicit peer list if not already provided.
: "${SAFE_EDGE_INITIAL_PEERS:=${SAFE_EDGE_NON_SAFETY_IP}:8011,${SAFE_EDGE_OWN_IP}:8030}"

DOCKER_ENV_ARGS=(
    -e "SAFE_EDGE_OWN_IP=${SAFE_EDGE_OWN_IP}"
    -e "SAFE_EDGE_NON_SAFETY_IP=${SAFE_EDGE_NON_SAFETY_IP}"
    -e "SAFE_EDGE_INITIAL_PEERS=${SAFE_EDGE_INITIAL_PEERS}"
)
[[ -n "${SAFE_EDGE_CROSS_DOMAIN_IP:-}" ]] && DOCKER_ENV_ARGS+=(-e "SAFE_EDGE_CROSS_DOMAIN_IP=${SAFE_EDGE_CROSS_DOMAIN_IP}")

CONTAINER_NAME="safe-edge-server"
docker rm -f "${CONTAINER_NAME}" 2>/dev/null || true

DOCKER_ARGS=(
    --name "${CONTAINER_NAME}"
    --network host
    "${DOCKER_ENV_ARGS[@]}"
)

if [[ -d /etc/safe-edge ]]; then
    DOCKER_ARGS+=(-v /etc/safe-edge:/etc/safe-edge:ro)
fi

echo "[server] Starting safe_edge_server (Ctrl+C to stop)"
echo "[server] OWN_IP=${SAFE_EDGE_OWN_IP} INITIAL_PEERS=${SAFE_EDGE_INITIAL_PEERS}"

exec docker run "${DOCKER_ARGS[@]}" "${IMAGE}"
