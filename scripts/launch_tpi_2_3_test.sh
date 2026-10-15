#!/usr/bin/env bash
# Run KPI/TPI 2.3 on Linux/Ubuntu.
# This KPI validates the shared common_server component and does not require QNX.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/test_output_common.sh"
LOG_DIR="${SCRIPT_DIR}/logs"
LOG_FILE="${LOG_DIR}/launch_tpi_2_3.log"

mkdir -p "${LOG_DIR}"
exec > >(tee "${LOG_FILE}") 2>&1

test_banner_open "TPI 2.3 - Common Server"
test_banner_context "linux" "${LOG_FILE}"
test_section "Running common_server test launcher"

TEST_RC=0
bash "${WORKSPACE_ROOT}/common_server/test/launch_server_common_test.sh" "$@" || TEST_RC=$?

test_artifact "Launcher log" "${LOG_FILE}"
test_footer "TPI 2.3 - Common Server" "${TEST_RC}" "${LOG_FILE}"
exit "${TEST_RC}"
