#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

OPT_NO_REBUILD=0
OPT_LINUX=0

usage() {
    cat <<EOF
Usage: bash scripts/launch_tests.sh [--no-rebuild] [--linux|--ubuntu] [-h|--help]

Runs the Docker integration tests and all available TPI test launchers.

Options:
  --no-rebuild   Forward --no-rebuild to TPI launchers that support it.
  --linux        Run QNX-oriented TPIs with native Linux binaries instead of QNX images.
  --ubuntu       Alias for --linux.
  -h, --help     Show this help.
EOF
}

for arg in "$@"; do
    case "${arg}" in
        --no-rebuild) OPT_NO_REBUILD=1 ;;
        --linux|--ubuntu) OPT_LINUX=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: ${arg}" >&2; usage >&2; exit 1 ;;
    esac
done

FAILURES=0

run_test() {
    local label="${1}"
    shift
    echo ""
    echo "=== ${label} ==="
    if "$@"; then
        echo "[launch_tests] PASS: ${label}"
    else
        echo "[launch_tests] FAIL: ${label}" >&2
        FAILURES=$((FAILURES + 1))
    fi
}

TPI_ARGS=()
if [[ "${OPT_NO_REBUILD}" -eq 1 ]]; then
    TPI_ARGS+=(--no-rebuild)
fi
if [[ "${OPT_LINUX}" -eq 1 ]]; then
    TPI_ARGS+=(--linux)
fi

run_test "Fast DDS server" bash "${SCRIPT_DIR}/launch_fast_server.sh" --test
run_test "Fast DDS edge" bash "${SCRIPT_DIR}/launch_fast_edge.sh" --test
run_test "TPI 2.3" bash "${SCRIPT_DIR}/launch_tpi_2_3_test.sh"
run_test "TPI 2.1" bash "${SCRIPT_DIR}/launch_tpi_2_1_test.sh" "${TPI_ARGS[@]}"
run_test "TPI 2.2" bash "${SCRIPT_DIR}/launch_tpi_2_2_test.sh" "${TPI_ARGS[@]}"
run_test "TPI 2.5" bash "${SCRIPT_DIR}/launch_tpi_2_5_test.sh" "${TPI_ARGS[@]}"
run_test "TPI 2.6" bash "${SCRIPT_DIR}/launch_tpi_2_6_test.sh" "${TPI_ARGS[@]}"

echo ""
if [[ "${FAILURES}" -eq 0 ]]; then
    echo "[launch_tests] All tests passed."
else
    echo "[launch_tests] ${FAILURES} test suite(s) failed." >&2
    exit 1
fi
