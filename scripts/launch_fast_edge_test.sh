#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE="safe-edge-edge:fast-test"
LOG_DIR="${SCRIPT_DIR}/logs"
LOG_FILE="${LOG_DIR}/launch_fast_edge_test.log"

usage() {
    cat <<EOF
Usage: bash launch_fast_edge_test.sh [-h|--help]

Runs the FastDDS edge integration test inside a Docker container.
Builds the test image first if it is not already present.

The test binary spawns safe_edge_edge_gateway as a subprocess and acts as a
mock server (publishing heartbeats and charger locations) to exercise the
edge health state machine via DDS on the loopback interface.

Log file: scripts/logs/launch_fast_edge_test.log
EOF
}

for arg in "$@"; do
    case "${arg}" in
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: ${arg}" >&2; usage >&2; exit 1 ;;
    esac
done

mkdir -p "${LOG_DIR}"

if ! docker image inspect "${IMAGE}" > /dev/null 2>&1; then
    echo "[launch_fast_edge_test] Image ${IMAGE} not found — building..."
    bash "${SCRIPT_DIR}/build_ubuntu.sh" --tests
fi

# Remove stale FastDDS shared-memory artifacts that can cause false failures.
sudo rm -f /dev/shm/fastdds_* /dev/shm/sem.fastdds_* 2>/dev/null || true

echo "[launch_fast_edge_test] Running edge integration test..."
echo "[launch_fast_edge_test] Log: ${LOG_FILE}"

set +e
docker run --rm --network host "${IMAGE}" 2>&1 | tee "${LOG_FILE}"
TEST_EXIT=${PIPESTATUS[0]}
set -e

if [[ ${TEST_EXIT} -eq 0 ]]; then
    echo "[launch_fast_edge_test] PASSED"
else
    echo "[launch_fast_edge_test] FAILED (exit ${TEST_EXIT})" >&2
fi

exit "${TEST_EXIT}"
