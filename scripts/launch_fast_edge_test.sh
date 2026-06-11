#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

LOG_DIR="${SCRIPT_DIR}/logs"
OPT_TEST=0

usage() {
    cat <<EOF
Usage: bash launch_fast_edge_test.sh [--test] [-h|--help]

Without --test  Runs safe_edge_edge_gateway as a persistent DDS service.
                Image: safe-edge-edge:fast
                Env vars (optional):
                  SAFE_EDGE_OWN_IP            IP this node announces (default: 127.0.0.1)
                  SAFE_EDGE_CROSS_DOMAIN_IP   IP of the cross-domain peer (default: 127.0.0.1)
                  SAFE_EDGE_SAFETY_IP         IP of the safety domain guest (default: 127.0.0.1)

--test          Runs the FastDDS edge integration test (self-contained, loopback only).
                Image: safe-edge-edge:fast-test
                Log: scripts/logs/launch_fast_edge_test.log
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
    IMAGE="safe-edge-edge:fast-test"
    LOG_FILE="${LOG_DIR}/launch_fast_edge_test.log"
    mkdir -p "${LOG_DIR}"

    if ! docker image inspect "${IMAGE}" > /dev/null 2>&1; then
        echo "[edge] Image ${IMAGE} not found — building..."
        bash "${SCRIPT_DIR}/build_ubuntu.sh" --tests
    fi

    echo "[edge] Running integration test..."
    echo "[edge] Log: ${LOG_FILE}"

    set +e
    docker run --rm --network host "${IMAGE}" 2>&1 | tee "${LOG_FILE}"
    TEST_EXIT=${PIPESTATUS[0]}
    set -e

    if [[ ${TEST_EXIT} -eq 0 ]]; then
        echo "[edge] PASSED"
    else
        echo "[edge] FAILED (exit ${TEST_EXIT})" >&2
    fi
    exit "${TEST_EXIT}"
fi

# Service mode
IMAGE="safe-edge-edge:fast"

if ! docker image inspect "${IMAGE}" > /dev/null 2>&1; then
    echo "[edge] Image ${IMAGE} not found — building..."
    bash "${SCRIPT_DIR}/build_ubuntu.sh"
fi

# Auto-detect virbr0 IP (Linux↔QEMU bridge) when not overridden.
: "${SAFE_EDGE_OWN_IP:=$(ip -o -f inet addr show virbr0 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)}"
: "${SAFE_EDGE_OWN_IP:=127.0.0.1}"
# Safety guest IP and non-safety guest IP are fixed by the hypervisor split configuration.
: "${SAFE_EDGE_SAFETY_IP:=192.168.10.2}"
: "${SAFE_EDGE_NON_SAFETY_IP:=192.168.20.2}"
# Build explicit peer list (8011 = cloud_gateway, lives in non-safety).
: "${SAFE_EDGE_INITIAL_PEERS:=${SAFE_EDGE_SAFETY_IP}:8001,${SAFE_EDGE_SAFETY_IP}:8002,${SAFE_EDGE_NON_SAFETY_IP}:8011,${SAFE_EDGE_OWN_IP}:8020}"

DOCKER_ENV_ARGS=(
    -e "SAFE_EDGE_OWN_IP=${SAFE_EDGE_OWN_IP}"
    -e "SAFE_EDGE_SAFETY_IP=${SAFE_EDGE_SAFETY_IP}"
    -e "SAFE_EDGE_NON_SAFETY_IP=${SAFE_EDGE_NON_SAFETY_IP}"
    -e "SAFE_EDGE_INITIAL_PEERS=${SAFE_EDGE_INITIAL_PEERS}"
)
[[ -n "${SAFE_EDGE_CROSS_DOMAIN_IP:-}" ]] && DOCKER_ENV_ARGS+=(-e "SAFE_EDGE_CROSS_DOMAIN_IP=${SAFE_EDGE_CROSS_DOMAIN_IP}")

CONTAINER_NAME="safe-edge-edge"
docker rm -f "${CONTAINER_NAME}" 2>/dev/null || true

echo "[edge] Starting safe_edge_edge_gateway (Ctrl+C to stop)"
echo "[edge] OWN_IP=${SAFE_EDGE_OWN_IP} INITIAL_PEERS=${SAFE_EDGE_INITIAL_PEERS}"

exec docker run --name "${CONTAINER_NAME}" --network host "${DOCKER_ENV_ARGS[@]}" "${IMAGE}"
