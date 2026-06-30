#!/usr/bin/env bash

test_banner_open() {
    local name="$1"
    echo
    echo "[==========] ${name}"
}

test_banner_context() {
    local platform="$1"
    local log_path="${2:-}"
    test_info "Platform: ${platform}"
    if [[ -n "${log_path}" ]]; then
        test_info "Logs: ${log_path}"
    fi
}

test_section() {
    local title="$1"
    echo
    echo "[----------] ${title}"
}

test_info() {
    local msg="$1"
    echo "[ INFO     ] ${msg}"
}

test_warn() {
    local msg="$1"
    echo "[ WARN     ] ${msg}" >&2
}

test_error() {
    local msg="$1"
    echo "[ ERROR    ] ${msg}" >&2
}

test_footer() {
    local name="$1"
    local rc="$2"
    local evidence="${3:-}"
    echo
    if [[ "${rc}" -eq 0 ]]; then
        echo "[==========] ${name} PASSED"
    else
        echo "[==========] ${name} FAILED (exit code ${rc})"
    fi
    if [[ -n "${evidence}" ]]; then
        test_info "Evidence: ${evidence}"
    fi
}

test_artifact() {
    local label="$1"
    local path="$2"
    test_info "${label}: ${path}"
}
