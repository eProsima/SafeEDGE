#!/usr/bin/env python3
"""SafeEDGE System Dashboard — lightweight HTTP server, no external dependencies."""

import collections
import http.server
import json
import os
import re
import subprocess
import sys
import threading
import time

STATUS_FILE = os.environ.get("SAFE_EDGE_STATUS_FILE", "/tmp/safe-edge-status.json")
INPUT_FILE  = os.environ.get("SAFE_EDGE_INPUT_FILE",  "/data/safe-edge-stage2/input.txt")
LOG_DIR     = os.environ.get("SAFE_EDGE_LOG_DIR",     "/tmp/safe_edge_vehicle_nodes")
PORT        = int(os.environ.get("SAFE_EDGE_WEB_PORT", "8080"))
HOST_IP     = os.environ.get("SAFE_EDGE_HOST_IP", "127.0.0.1")
GUEST_IP    = os.environ.get("SAFE_EDGE_GUEST_IP", HOST_IP)
PLATFORM    = os.environ.get("SAFE_EDGE_PLATFORM", "linux")

STATUS_POLL_HZ = 4
_status_cache: bytes = b""
_status_lock  = threading.Lock()

# ── Latency measurement ────────────────────────────────────────────────────────
_LAT_WINDOW  = 100
_lat_lock    = threading.Lock()
_lat_data    = {
    "e2e":      {"last": None, "p50": None, "p90": None, "p95": None, "p99": None, "n": 0},
    "reaction": {"last": None, "p50": None, "p90": None, "p95": None, "p99": None, "n": 0},
}
_e2e_samples      = collections.deque(maxlen=_LAT_WINDOW)
_reaction_samples = collections.deque(maxlen=_LAT_WINDOW)

# ── External load stressors ───────────────────────────────────────────────────
_load_lock = threading.Lock()
_load_procs: list = []  # active subprocess.Popen stressors

# Host-side CPU + I/O pressure, spread across cores 0-3.
# In QNX mode this starves QEMU vCPUs via host scheduler contention.
_LOAD_CMDS = [
    ["taskset", "-c", "0", "bash", "-c", "while true; do :; done"],
    ["taskset", "-c", "1", "bash", "-c", "while true; do :; done"],
    ["taskset", "-c", "2", "bash", "-c", "dd if=/dev/urandom of=/dev/null bs=64k"],
    ["taskset", "-c", "3", "bash", "-c", "dd if=/dev/urandom of=/dev/null bs=64k"],
]

_VM_PUB_RE = re.compile(r'\[vehicle_mock\] Published SafetyInputFrame t_pub=(\d+)\.(\d{1,9})')
_VM_DEC_RE = re.compile(r'\[vehicle_mock\] Received PolicyDecision t_rx_dec=(\d+)\.(\d{1,9})')
_PE_RX_RE  = re.compile(r'\[policy_engine\] Received SafetyInputFrame t_rx=(\d+)\.(\d{1,9})')
_PE_DEC_RE = re.compile(r'\[policy_engine\] Published PolicyDecision t_dec=(\d+)\.(\d{1,9})')


def _ts_to_ns(s, frac):
    # frac is tv_nsec printed without zero-padding, already in nanoseconds
    return int(s) * 1_000_000_000 + int(frac)


def _percentile(data, p):
    if not data:
        return None
    s = sorted(data)
    idx = max(0, int(len(s) * p / 100) - 1)
    return round(s[idx], 3)


def _update_lat_stats():
    with _lat_lock:
        for key, samples in (("e2e", _e2e_samples), ("reaction", _reaction_samples)):
            if samples:
                _lat_data[key]["last"] = round(samples[-1], 3)
                _lat_data[key]["p50"]  = _percentile(samples, 50)
                _lat_data[key]["p90"]  = _percentile(samples, 90)
                _lat_data[key]["p95"]  = _percentile(samples, 95)
                _lat_data[key]["p99"]  = _percentile(samples, 99)
                _lat_data[key]["n"]    = len(samples)


def _read_new_lines(path, pos):
    try:
        with open(path, "r", errors="replace") as f:
            f.seek(0, 2)
            size = f.tell()
            if size < pos:
                pos = 0  # file rotated/truncated
            f.seek(pos)
            lines = f.readlines()
            return lines, f.tell()
    except OSError:
        return [], pos


def _log_end_pos(path):
    try:
        with open(path, "r", errors="replace") as f:
            f.seek(0, 2)
            return f.tell()
    except OSError:
        return 0


def _poll_latency_logs():
    vm_log = os.path.join(LOG_DIR, "safe_edge_vehicle_mock.log")
    pe_log = os.path.join(LOG_DIR, "safe_edge_policy_engine.log")
    # Start at EOF — skip any historical log data from previous runs
    vm_pos = _log_end_pos(vm_log)
    pe_pos = _log_end_pos(pe_log)
    vm_pending = None  # t_pub ns for pending E2E pair
    pe_pending = None  # t_rx  ns for pending reaction pair
    interval = 0.25
    while True:
        vm_lines, vm_pos = _read_new_lines(vm_log, vm_pos)
        for line in vm_lines:
            m = _VM_PUB_RE.search(line)
            if m:
                vm_pending = _ts_to_ns(m.group(1), m.group(2))
                continue
            m = _VM_DEC_RE.search(line)
            if m and vm_pending is not None:
                t_rx = _ts_to_ns(m.group(1), m.group(2))
                delta_ms = (t_rx - vm_pending) / 1_000_000.0
                if 0.0 < delta_ms < 10_000.0:
                    _e2e_samples.append(delta_ms)
                vm_pending = None

        pe_lines, pe_pos = _read_new_lines(pe_log, pe_pos)
        for line in pe_lines:
            m = _PE_RX_RE.search(line)
            if m:
                pe_pending = _ts_to_ns(m.group(1), m.group(2))
                continue
            m = _PE_DEC_RE.search(line)
            if m and pe_pending is not None:
                t_dec = _ts_to_ns(m.group(1), m.group(2))
                delta_ms = (t_dec - pe_pending) / 1_000_000.0
                if 0.0 < delta_ms < 10_000.0:
                    _reaction_samples.append(delta_ms)
                pe_pending = None

        _update_lat_stats()
        time.sleep(interval)

def _poll_status_file():
    global _status_cache
    interval = 1.0 / STATUS_POLL_HZ
    while True:
        try:
            with open(STATUS_FILE, "rb") as f:
                data = f.read()
            with _status_lock:
                _status_cache = data
        except OSError:
            with _status_lock:
                _status_cache = b""
        time.sleep(interval)

INPUT_FIELDS = [
    ("soc",                   float, 0.0,   100.0),
    ("emergency_stop",        int,   0,     1),
    ("adas_fault",            int,   0,     1),
    ("v2g_ready",             int,   0,     1),
    ("speed_mps",             float, 0.0,   50.0),
    ("braking_available",     int,   0,     1),
    ("steering_available",    int,   0,     1),
]

INPUT_DEFAULTS = {
    "soc": 50.0, "emergency_stop": 0, "adas_fault": 0,
    "v2g_ready": 0, "speed_mps": 0.0,
    "braking_available": 1, "steering_available": 1,
}

HTML = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>SafeEDGE Dashboard</title>
<style>
  @font-face { font-family: 'Roboto'; src: local('Roboto'), local('Roboto-Regular'); font-weight: 400; }
  @font-face { font-family: 'Roboto'; src: local('Roboto Bold'), local('Roboto-Bold'); font-weight: 700; }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html { font-size: 2.0vh; }
  body { font-family: 'Roboto', sans-serif; background: #ffffff; color: #24292f; padding: 1em 1.5em; margin: 0 auto; width: fit-content; min-width: 50em; }

  h1 { font-size: 2.1em; color: #09487e; margin-bottom: 0.2em; text-align: center; }
  .subtitle { font-size: 0.78em; color: #57606a; margin-bottom: 1em; }
  #status-error { color: #cf222e; font-size: 0.8em; margin-bottom: 0.5em; }

  /* Topology stack */
  .topology { display: flex; flex-direction: column; }

  /* Generic layer block */
  .layer-block {
    border: 4px solid #09487e; border-radius: 0.5em;
    padding: 0.75em 0.9em; background: #f6f8fa;
    transition: border-color 0.3s, background 0.3s;
  }
  .layer-block.block-ok  { border-color: #2da44e; background: #e6ffed; }
  .layer-block.block-err { border-color: #da3633; background: #fff5f5; }
  #server-block:not(.block-ok):not(.block-err),
  #edge-block:not(.block-ok):not(.block-err)   { background: #eaf2f8; }
  .block-header {
    display: flex; align-items: flex-start; justify-content: space-between;
    margin-bottom: 0.6em; padding-bottom: 0.8em;
  }
  .layer-title {
    font-size: 1.2em; font-weight: bold; text-transform: uppercase; letter-spacing: 0.08em;
  }
  .server-title, .edge-title, .vehicle-title { color: #09487e; }

  /* Node cards */
  .node-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 0.4em; }
  .card {
    padding: 0.6em 0.75em; border-radius: 0.4em; border: 1px solid;
    font-size: 0.82em; display: flex; flex-direction: column; gap: 0.2em;
  }
  .card-header { display: flex; align-items: center; gap: 0.4em; }
  .dot { width: 0.5em; height: 0.5em; border-radius: 50%; flex-shrink: 0; }
  .card .name { font-weight: bold; color: #24292f; }
  .card .detail { font-size: 0.8em; color: #57606a; padding-left: 0.9em; }
  .card.green .detail { color: #2da44e; }
  .card.red   .detail { color: #cf222e; }
  .card.green { border-color: #2da44e; background: #e6ffed; }
  .card.red   { border-color: #da3633; background: #fff5f5; }
  .green .dot { background: #2da44e; box-shadow: 0 0 0.3em #2da44e66; }
  .red   .dot { background: #cf222e; box-shadow: 0 0 0.3em #cf222e66; }

  /* Colored link bars */
  .link-row {
    display: grid; grid-template-columns: 1fr 7em 1fr;
    height: 1.5em; align-items: stretch;
  }
  .edge-row {
    display: grid; grid-template-columns: 1fr 7em 1fr;
    align-items: stretch;
  }
  .bar-cell { display: flex; justify-content: center; align-items: stretch; }
  .lvbar { width: 0.25em; border-radius: 0.15em; min-height: 1.5em; height: 100%; }
  .lhbar { height: 0.25em; border-radius: 0.15em; width: 80%; }
  .bar-ok  { background: #2da44e; box-shadow: 0 0 0.25em #2da44e66; }
  .bar-err { background: #cf222e; box-shadow: 0 0 0.25em #cf222e66; }
  .bar-none { background: #8c959f; }

  /* Vehicle container */
  .vehicle-container {
    border: 4px solid #09487e; border-radius: 0.6em;
    background: #eaf2f8; padding: 0.9em 1em;
  }
  .vehicle-inner {
    display: grid; grid-template-columns: 1fr 7em 1fr;
    gap: 0 0.5em; align-items: start;
  }
  .domain-label {
    font-size: 0.82em; font-weight: bold; text-transform: uppercase;
    letter-spacing: 0.08em; color: #09487e; margin-bottom: 0.5em;
    border-bottom: 2px solid #09487e; padding-bottom: 0.2em;
  }
  .vehicle-inner .node-grid { grid-template-columns: repeat(3, 1fr); gap: 0.3em; }
  .vehicle-inner .card { font-size: 0.76em; padding: 0.4em 0.5em; }
  .safety-link-col {
    display: flex; flex-direction: column; align-items: center;
    justify-content: center; align-self: stretch; padding-top: 1.8em;
  }

  /* Vehicle Mock Input */
  .vehicle-form-wrap { margin-top: 0; }
  .vehicle-form-label {
    font-size: 0.82em; font-weight: bold; text-transform: uppercase;
    letter-spacing: 0.08em; color: #09487e; margin-bottom: 0.4em;
    border-bottom: 2px solid #09487e; padding-bottom: 0.2em;
  }
  form { display: grid; grid-template-columns: 1fr auto; gap: 0.25em 0.3em; align-items: center; }
  form label { font-size: 0.76em; color: #09487e; }
  form input {
    background: #e8f4fd; border: 1px solid #09487e; color: #09487e;
    padding: 0.2em 0.3em; border-radius: 0.25em; font-family: monospace; font-size: 0.8em; width: var(--field-w); box-sizing: border-box;
  }
  form input::placeholder { color: #2b6cb0; opacity: 0.6; }
  form input:focus { outline: none; border-color: #0895cd; }
  .apply-btn {
    grid-column: 1 / -1; margin-top: 0.25em; padding: 0.4em; width: 100%;
    background: #0895cd; color: #ffffff; border: 2px solid #09487e; border-radius: 0.25em;
    cursor: pointer; font-family: 'Roboto', sans-serif; font-size: 0.82em; font-weight: bold;
  }
  .apply-btn:hover:not(:disabled) { background: #09487e; }
  .apply-btn:disabled { opacity: 0.5; cursor: default; }
  #apply-result { font-size: 0.6em; position: absolute; white-space: nowrap; }

  /* Section labels (inside vehicle) */
  .section-label {
    font-size: 0.82em; font-weight: bold; text-transform: uppercase;
    letter-spacing: 0.08em; color: #09487e; margin: 0.9em 0 0.4em;
    border-bottom: 2px solid #09487e; padding-bottom: 0.25em;
  }
  .policy-row { display: flex; flex-direction: column; align-items: flex-start; gap: 0.3em; margin-top: 0.25em; }
  .policy-badge { padding: 0.2em 0.6em; border-radius: 0.25em; font-size: 0.72em; font-weight: bold; white-space: nowrap; }
  .nominal          { background: #e6ffed; color: #1a7f37; border: 1px solid #2da44e; }
  .degraded-partial { background: #fff8c5; color: #9a6700; border: 1px solid #bf8700; }
  .degraded-full    { background: #fff5f5; color: #cf222e; border: 1px solid #da3633; }
  .unknown          { background: #f6f8fa; color: #09487e; border: 1px solid #09487e; }
  .policy-meta { font-size: 0.78em; color: #09487e; }

  /* Latency table */
  .lat-table { width: 100%; border-collapse: collapse; font-size: 0.82em; table-layout: fixed; }
  .lat-table th {
    color: #09487e; font-weight: bold; text-align: right;
    padding: 0.12em 0.25em 0.12em 0; border-bottom: 2px solid #09487e; width: 18%;
  }
  .lat-table th:first-child { text-align: left; width: 28%; }
  .lat-table td { padding: 0.2em 0.25em 0.2em 0; color: #09487e; font-variant-numeric: tabular-nums; text-align: right; }
  .lat-table td.metric { color: #09487e; text-align: left; }
  .lat-na  { color: #2b6cb0; opacity: 0.5; }
  .lat-err { color: #cf222e; }

  :root { --field-w: 7rem; }

  /* Docker inline buttons (inside server/edge blocks) */
  .docker-inline-btn {
    width: var(--field-w); padding: 0.25em 0.4em; border-radius: 0.25em; cursor: pointer;
    font-family: 'Roboto', sans-serif; font-size: 0.75em; font-weight: bold;
    transition: opacity 0.15s; white-space: nowrap; text-align: center;
  }
  .docker-inline-btn:disabled { opacity: 0.5; cursor: default; }
  .docker-inline-btn { background: #0895cd; color: #ffffff; border: 2px solid #09487e; }
  .docker-inline-btn:hover:not(:disabled) { background: #09487e; }
  .docker-result { font-size: 0.6em; text-align: right; position: absolute; right: 0; top: 100%; white-space: nowrap; }
  #load-result { font-size: 0.6em; position: absolute; white-space: nowrap; }

  /* Load button (inside vehicle) */
  .load-btn {
    padding: 0.4em 0.4em; border-radius: 0.25em; cursor: pointer;
    font-family: 'Roboto', sans-serif; font-size: 0.82em; font-weight: bold;
    transition: opacity 0.15s; width: 100%;
  }
  .load-btn:disabled { opacity: 0.5; cursor: default; }
  .load-btn { background: #0895cd; color: #ffffff; border: 2px solid #09487e; }
  .load-btn:hover:not(:disabled) { background: #09487e; }
  .inline-btn { white-space: nowrap; width: var(--field-w); padding: 0.12em 0.4em; font-size: 0.75em; font-weight: normal; text-align: center; }

  .ok  { color: #1a7f37; }
  .err { color: #cf222e; }
</style>
</head>
<body>
<h1>SafeEDGE System Dashboard</h1>

<div class="topology">

  <!-- ── Server ──────────────────────────────────────────────── -->
  <div class="layer-block" id="server-block">
    <div class="block-header">
      <div class="layer-title server-title">Server</div>
      <div style="position:relative">
        <button id="docker-btn-server" class="docker-inline-btn running"
                onclick="toggleDocker('server','Server',this)">■ Stop Server</button>
        <div class="docker-result" id="docker-result-server"></div>
      </div>
    </div>
    <div id="server-nodes"></div>
  </div>

  <!-- server_link: left leg → Edge | right leg → Non-Safety -->
  <div class="link-row">
    <div class="bar-cell"><div id="srv-lnk-l" class="lvbar bar-ok"></div></div>
    <div></div>
    <div class="bar-cell"><div id="srv-lnk-r" class="lvbar bar-ok"></div></div>
  </div>

  <!-- Edge (left-aligned) + right bar continues server→Non-Safety -->
  <div class="edge-row">
    <div>
      <div class="layer-block" id="edge-block">
        <div class="block-header">
          <div class="layer-title edge-title">Edge</div>
          <div style="position:relative">
            <button id="docker-btn-edge" class="docker-inline-btn running"
                    onclick="toggleDocker('edge','Edge',this)">■ Stop Edge</button>
            <div class="docker-result" id="docker-result-edge"></div>
          </div>
        </div>
        <div id="edge-nodes"></div>
      </div>
    </div>
    <div></div>
    <div class="bar-cell"><div id="srv-ns-mid" class="lvbar bar-ok"></div></div>
  </div>

  <!-- edge_link (left) + server→Non-Safety continuation (right) -->
  <div class="link-row">
    <div class="bar-cell"><div id="edge-lnk" class="lvbar bar-ok"></div></div>
    <div></div>
    <div class="bar-cell"><div id="srv-ns-bot" class="lvbar bar-ok"></div></div>
  </div>

  <!-- ── Vehicle ─────────────────────────────────────────────── -->
  <div class="vehicle-container">
    <div class="layer-title vehicle-title" style="margin-bottom:0.75em;">Vehicle</div>

    <!-- Safety | safety_link bar | Non-Safety -->
    <div class="vehicle-inner">
      <!-- Row 1: node grids -->
      <div>
        <div class="domain-label">Safety</div>
        <div class="node-grid" id="safety-grid"></div>
      </div>
      <div class="safety-link-col">
        <div id="safety-lnk" class="lhbar bar-ok"></div>
      </div>
      <div>
        <div class="domain-label">Non-Safety</div>
        <div class="node-grid" id="non-safety-grid"></div>
      </div>

      <!-- Separator -->
      <div style="grid-column:1/-1; border-top: 2px solid #09487e; margin: 0.5em 0;"></div>

      <!-- Row 2: controls -->
      <div class="vehicle-form-wrap">
        <div class="vehicle-form-label" style="display:flex;align-items:center;gap:0.5em;">
          <span style="flex:1">Vehicle Mock Input</span>
          <button id="apply-vehicle-btn" class="load-btn inline-btn" onclick="applyVehicle()">Apply</button>
        </div>
        <form id="vehicle-form"></form>
        <div style="position:relative"><div id="apply-result"></div></div>
      </div>
      <div></div>
      <div>
        <!-- Load Stress -->
        <div class="section-label" style="display:flex;align-items:center;gap:0.5em;">
          <span style="flex:1">Load Stress</span>
          <div style="position:relative;padding-bottom:0.8em;">
            <button id="load-btn" class="load-btn inline-btn" onclick="toggleLoad()">▶ Apply Load</button>
            <div id="load-result"></div>
          </div>
        </div>

        <!-- Latency -->
        <div class="section-label" style="display:flex;align-items:center;gap:0.5em;">
          Latency
          <span id="lat-n" style="font-weight:normal;text-transform:none;letter-spacing:0;color:#2b6cb0;font-size:0.9em;flex:1"></span>
          <button id="lat-reset-btn" class="load-btn inline-btn" onclick="resetLatency()" disabled>↺ Reset</button>
        </div>
        <table class="lat-table">
          <thead><tr><th></th><th>Last</th><th>P50</th><th>P90</th><th>P95</th><th>P99</th></tr></thead>
          <tbody>
            <tr>
              <td class="metric">E2E</td>
              <td id="lat-e2e-last" class="lat-na">—</td>
              <td id="lat-e2e-p50"  class="lat-na">—</td>
              <td id="lat-e2e-p90"  class="lat-na">—</td>
              <td id="lat-e2e-p95"  class="lat-na">—</td>
              <td id="lat-e2e-p99"  class="lat-na">—</td>
            </tr>
            <tr>
              <td class="metric">Reaction</td>
              <td id="lat-rxn-last" class="lat-na">—</td>
              <td id="lat-rxn-p50"  class="lat-na">—</td>
              <td id="lat-rxn-p90"  class="lat-na">—</td>
              <td id="lat-rxn-p95"  class="lat-na">—</td>
              <td id="lat-rxn-p99"  class="lat-na">—</td>
            </tr>
          </tbody>
        </table>

        <!-- Policy -->
        <div class="section-label">Policy</div>
        <div id="policy-area"></div>
      </div>
    </div>

  </div>

</div><!-- /topology -->

<script>
const FIELDS = [
  ["soc",                    "float", 0,   100,  "SOC (%)",              50],
  ["emergency_stop",         "int",   0,   1,    "Emergency Stop (0/1)",  0],
  ["adas_fault",             "int",   0,   1,    "ADAS Fault (0/1)",      0],
  ["v2g_ready",              "int",   0,   1,    "V2G Ready (0/1)",       0],
  ["speed_mps",              "float", 0,   50,   "Speed (m/s)",           0],
  ["braking_available",      "int",   0,   1,    "Braking (0/1)",         1],
  ["steering_available",     "int",   0,   1,    "Steering (0/1)",        1],
];

const SAFETY_NODES     = new Set(["vehicle_mock", "policy_engine", "safety_io_adapters"]);
const SAFETY_ORDER     = ["vehicle_mock", "safety_io_adapters", "policy_engine"];
const NON_SAFETY_NODES = new Set(["cloud_gateway", "ota_service", "infotainment"]);

const DEBOUNCE_TICKS = 3;
const _debounceState = {};
function debounced(key, value) {
  if (!(key in _debounceState)) {
    _debounceState[key] = { confirmed: value, pending: value, count: DEBOUNCE_TICKS };
    return value;
  }
  const s = _debounceState[key];
  if (value === s.pending) {
    if (s.count < DEBOUNCE_TICKS) s.count++;
    if (s.count >= DEBOUNCE_TICKS) s.confirmed = value;
  } else { s.pending = value; s.count = 1; }
  return s.confirmed;
}

// Build vehicle form
const form = document.getElementById("vehicle-form");
FIELDS.forEach(([name, , , , label, def]) => {
  const lbl = document.createElement("label");
  lbl.textContent = label; lbl.htmlFor = "f_" + name;
  const inp = document.createElement("input");
  inp.type = "text"; inp.id = "f_" + name; inp.name = name; inp.placeholder = String(def);
  form.appendChild(lbl);
  form.appendChild(inp);
});

function card(name, ok, detail) {
  const cls = ok ? "green" : "red";
  return `<div class="card ${cls}">
    <div class="card-header"><span class="dot"></span><span class="name">${name}</span></div>
    <div class="detail">${detail || (ok ? "alive" : "dead")}</div>
  </div>`;
}

function setBar(id, ok) {
  const el = document.getElementById(id);
  if (!el) return;
  el.className = el.className.replace(/\b(bar-ok|bar-err|bar-none)\b/g, "").trim();
  el.classList.add(ok ? "bar-ok" : "bar-err");
}

function policyBadge(mode) {
  let cls = "unknown";
  if (mode === "POLICY_NOMINAL") cls = "nominal";
  else if (mode === "POLICY_DEGRADED_COMPLETE") cls = "degraded-full";
  else if (mode && mode !== "POLICY_UNKNOWN") cls = "degraded-partial";
  return `<span class="policy-badge ${cls}">${mode || "UNKNOWN"}</span>`;
}

const dockerState = { server: true, edge: true };

function renderDockerState() {
  ["server", "edge"].forEach(id => {
    const running = dockerState[id];
    const label = id.charAt(0).toUpperCase() + id.slice(1);
    const btn = document.getElementById("docker-btn-" + id);
    if (btn) {
      btn.textContent = running ? `■ Stop ${label}` : `▶ Start ${label}`;
    }
    const nodesEl = document.getElementById(id + "-nodes");
    if (nodesEl) nodesEl.innerHTML =
      card(id, running, running ? "container running" : "container stopped");
  });
}

const _ALL_BARS = ["srv-lnk-l","srv-lnk-r","srv-ns-mid","srv-ns-bot","edge-lnk","safety-lnk"];
let _lastSafetyNames    = [];
let _lastNonSafetyNames = [];

const _CTRL_BTNS = ["docker-btn-server", "docker-btn-edge", "load-btn", "lat-reset-btn", "apply-vehicle-btn"];
function _setButtonsEnabled(on) {
  _CTRL_BTNS.forEach(id => {
    const el = document.getElementById(id); if (el) el.disabled = !on;
  });
}

function clearLatency() {
  ["lat-e2e-last","lat-e2e-p50","lat-e2e-p90","lat-e2e-p95","lat-e2e-p99",
   "lat-rxn-last","lat-rxn-p50","lat-rxn-p90","lat-rxn-p95","lat-rxn-p99"].forEach(id => {
    const el = document.getElementById(id);
    if (el) { el.textContent = "—"; el.className = "lat-na"; }
  });
  const nEl = document.getElementById("lat-n");
  if (nEl) nEl.textContent = "";
  const btn = document.getElementById("lat-reset-btn");
  if (btn) btn.disabled = true;
}

function clearVehicleInputs() {
  FIELDS.forEach(([name]) => {
    const el = document.getElementById("f_" + name);
    if (el) el.value = "";
  });
}

function setAllError() {
  _setButtonsEnabled(false);
  _ALL_BARS.forEach(id => setBar(id, false));
  ["server", "edge"].forEach(id => {
    const el = document.getElementById(id + "-nodes");
    if (el) el.innerHTML = card(id, false, "unreachable");
  });
  document.getElementById("safety-grid").innerHTML =
    _lastSafetyNames.map(n => card(n, false)).join("");
  document.getElementById("non-safety-grid").innerHTML =
    _lastNonSafetyNames.map(n => card(n, false)).join("");
  document.getElementById("policy-area").innerHTML =
    `<div class="policy-row">${policyBadge(null)}</div>`;
  clearLatency();
  clearVehicleInputs();
}

function refresh() {
  fetch("/status")
    .then(r => r.ok ? r.json() : Promise.reject(r.status))
    .then(data => {
      _setButtonsEnabled(true);
      renderDockerState();
      // Node grids
      const nodes = data.nodes || [];
      const safetyNodes = nodes.filter(n => SAFETY_NODES.has(n.name))
             .sort((a, b) => SAFETY_ORDER.indexOf(a.name) - SAFETY_ORDER.indexOf(b.name));
      const nonSafetyNodes = nodes.filter(n => NON_SAFETY_NODES.has(n.name));
      _lastSafetyNames    = safetyNodes.map(n => n.name);
      _lastNonSafetyNames = nonSafetyNodes.map(n => n.name);
      document.getElementById("safety-grid").innerHTML =
        safetyNodes.map(n => card(n.name, debounced(n.name, n.alive))).join("");
      document.getElementById("non-safety-grid").innerHTML =
        nonSafetyNodes.map(n => card(n.name, debounced(n.name, n.alive))).join("");

      // Link bars
      const byName = {};
      (data.communications || []).forEach(c => { byName[c.name] = c; });
      const slOk = debounced("comm_server_link", (byName["server_link"] || {ok:false}).ok);
      const elOk = debounced("comm_edge_link",   (byName["edge_link"]   || {ok:false}).ok);
      const sfOk = debounced("comm_safety_link", (byName["safety_link"] || {ok:false}).ok);
      setBar("srv-lnk-l",  slOk);
      setBar("srv-lnk-r",  slOk);
      setBar("srv-ns-mid", slOk);
      setBar("srv-ns-bot", slOk);
      setBar("edge-lnk",   elOk);
      setBar("safety-lnk", sfOk);

      // Policy
      const p = data.policy || {};
      const pmeta = [];
      if (p.reason) pmeta.push(`reason: ${p.reason}`);
      document.getElementById("policy-area").innerHTML =
        `<div class="policy-row">${policyBadge(p.mode)}` +
        `<span class="policy-meta">${pmeta.join(" &nbsp;·&nbsp; ")}</span></div>`;

      // Latency
      const lat = data.latency || {};
      const e = lat.e2e || {}, r = lat.reaction || {};
      const E2E_THR = 20, RXN_THR = 100;
      setCell("lat-e2e-last", e.last, E2E_THR); setCell("lat-e2e-p50", e.p50, E2E_THR);
      setCell("lat-e2e-p90",  e.p90,  E2E_THR); setCell("lat-e2e-p95", e.p95, E2E_THR);
      setCell("lat-e2e-p99",  e.p99,  E2E_THR);
      setCell("lat-rxn-last", r.last, RXN_THR); setCell("lat-rxn-p50", r.p50, RXN_THR);
      setCell("lat-rxn-p90",  r.p90,  RXN_THR); setCell("lat-rxn-p95", r.p95, RXN_THR);
      setCell("lat-rxn-p99",  r.p99,  RXN_THR);
      const n = Math.max(e.n || 0, r.n || 0);
      const nEl = document.getElementById("lat-n");
      if (nEl) nEl.textContent = `(${n}/100 samples)`;
      const resetBtn = document.getElementById("lat-reset-btn");
      if (resetBtn) resetBtn.disabled = n < 100;
    })
    .catch(() => setAllError());
}

function applyVehicle() {
  const payload = {};
  let valid = true;
  FIELDS.forEach(([name, type, min, max]) => {
    const raw = document.getElementById("f_" + name).value.trim();
    if (raw === "") return;
    const val = type === "int" ? parseInt(raw, 10) : parseFloat(raw);
    if (isNaN(val) || val < min || val > max) {
      document.getElementById("apply-result").innerHTML =
        `<span class="err">Invalid value for ${name}: ${raw} (range [${min}, ${max}])</span>`;
      valid = false;
    } else { payload[name] = val; }
  });
  if (!valid) return;
  fetch("/vehicle", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload) })
  .then(r => {
    const el = document.getElementById("apply-result");
    if (r.ok) {
      el.innerHTML = '<span class="ok">Applied successfully.</span>';
      setTimeout(() => { el.innerHTML = ""; }, 2000);
    } else { r.text().then(t => { el.innerHTML = `<span class="err">Error ${r.status}: ${t}</span>`; }); }
  }).catch(e => {
    document.getElementById("apply-result").innerHTML = `<span class="err">Request failed: ${e}</span>`;
  });
}

function toggleDocker(id, label, btn) {
  const running = dockerState[id];
  const action  = running ? `stop_${id}` : `start_${id}`;
  const resultEl = document.getElementById("docker-result-" + id);
  btn.disabled = true;
  if (running) { btn.textContent = `⏳ Stopping…`; }
  fetch("/control", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ action }) })
  .then(r => {
    btn.disabled = false;
    if (r.ok || r.status === 504) {
      dockerState[id] = !running;
      renderDockerState();
      if (resultEl) {
        resultEl.innerHTML = `<span class="ok">${label} ${dockerState[id] ? "started" : "stopped"}.</span>`;
        setTimeout(() => { resultEl.innerHTML = ""; }, 3000);
      }
    } else {
      renderDockerState();
      r.text().then(t => { if (resultEl) resultEl.innerHTML = `<span class="err">Error: ${t}</span>`; });
    }
  }).catch(e => {
    btn.disabled = false;
    renderDockerState();
    if (resultEl) resultEl.innerHTML = `<span class="err">Request failed: ${e}</span>`;
  });
}

let _loadActive = false;
function toggleLoad() {
  const btn = document.getElementById("load-btn");
  const el  = document.getElementById("load-result");
  btn.disabled = true;
  fetch("/control", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ action: _loadActive ? "stop_load" : "start_load" }) })
  .then(r => {
    btn.disabled = false;
    if (r.ok || r.status === 504) {
      _loadActive = !_loadActive;
      btn.textContent = _loadActive ? "■ Remove Load" : "▶ Apply Load";
      el.innerHTML = `<span class="ok">Load ${_loadActive ? "started" : "stopped"}.</span>`;
      setTimeout(() => { el.innerHTML = ""; }, 2000);
    } else { r.text().then(t => { el.innerHTML = `<span class="err">Error: ${t}</span>`; }); }
  }).catch(e => { btn.disabled = false; el.innerHTML = `<span class="err">Request failed: ${e}</span>`; });
}

function fmt(v) { return v === null || v === undefined ? "—" : Number(v).toFixed(2); }
function setCell(id, v, threshold) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = fmt(v);
  if (v === null || v === undefined) el.className = "lat-na";
  else if (threshold !== undefined && v > threshold) el.className = "lat-err";
  else el.className = "";
}

function resetLatency() {
  fetch("/control", { method: "POST", headers: {"Content-Type":"application/json"},
    body: JSON.stringify({ action: "reset_latency" }) });
}

renderDockerState();
refresh();
setInterval(refresh, 250);
</script>
</body>
</html>
"""

def _read_current_input():
    vals = dict(INPUT_DEFAULTS)
    try:
        with open(INPUT_FILE, "r") as f:
            for line in f:
                line = line.strip()
                if "=" not in line:
                    continue
                k, _, v = line.partition("=")
                k = k.strip()
                v = v.strip()
                if k in vals:
                    try:
                        vals[k] = float(v) if "." in v else int(v)
                    except ValueError:
                        pass
    except OSError:
        pass
    return vals


def _write_input(vals):
    lines = []
    for name, typ, lo, hi in INPUT_FIELDS:
        raw = vals.get(name, INPUT_DEFAULTS[name])
        if typ is int:
            lines.append(f"{name}={int(raw)}")
        else:
            lines.append(f"{name}={float(raw):.6g}")
    content = "\n".join(lines) + "\n"
    tmp = INPUT_FILE + ".tmp"
    with open(tmp, "w") as f:
        f.write(content)
    os.replace(tmp, INPUT_FILE)


class Handler(http.server.BaseHTTPRequestHandler):

    def log_message(self, fmt, *args):
        pass  # suppress default access log

    def _send(self, code, content_type, body):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            page = HTML.replace("%(status_file)s", STATUS_FILE)
            self._send(200, "text/html; charset=utf-8", page)
        elif self.path == "/status":
            with _status_lock:
                data = _status_cache
            if not data:
                self._send(503, "text/plain", "Status file not available yet")
                return
            try:
                obj = json.loads(data)
            except ValueError:
                obj = {}
            with _lat_lock:
                obj["latency"] = dict(_lat_data)
            self._send(200, "application/json", json.dumps(obj).encode())
        else:
            self._send(404, "text/plain", "Not found")

    def do_POST(self):
        if self.path == "/control":
            self._handle_control()
            return
        if self.path != "/vehicle":
            self._send(404, "text/plain", "Not found")
            return

        length = int(self.headers.get("Content-Length", 0))
        if length > 4096:
            self._send(400, "text/plain", "Request too large")
            return

        raw = self.rfile.read(length)
        try:
            body = json.loads(raw)
        except json.JSONDecodeError as e:
            self._send(400, "text/plain", f"Invalid JSON: {e}")
            return

        if not isinstance(body, dict):
            self._send(400, "text/plain", "Expected JSON object")
            return

        # Validate provided fields
        validated = _read_current_input()
        for name, typ, lo, hi in INPUT_FIELDS:
            if name not in body:
                continue
            try:
                val = typ(body[name])
            except (ValueError, TypeError):
                self._send(400, "text/plain", f"Invalid type for {name}")
                return
            if not (lo <= val <= hi):
                self._send(400, "text/plain",
                           f"Value for {name} out of range [{lo}, {hi}]: {val}")
                return
            validated[name] = val

        try:
            os.makedirs(os.path.dirname(INPUT_FILE), exist_ok=True)
            _write_input(validated)
        except OSError as e:
            self._send(500, "text/plain", f"Write failed: {e}")
            return

        self._send(200, "text/plain", "OK")

    def _handle_control(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length)
        try:
            body = json.loads(raw)
        except json.JSONDecodeError:
            self._send(400, "text/plain", "Invalid JSON")
            return
        action = body.get("action", "")
        try:
            if action == "stop_server":
                subprocess.run(["docker", "rm", "-f", "safe-edge-server"], timeout=5, check=False)
                self._send(200, "text/plain", "OK")
            elif action == "start_server":
                subprocess.run(["docker", "rm", "-f", "safe-edge-server"], timeout=5, check=False)
                subprocess.run([
                    "docker", "run", "-d", "--name", "safe-edge-server", "--network", "host",
                    "-e", f"SAFE_EDGE_OWN_IP={HOST_IP}",
                    "-e", f"SAFE_EDGE_NON_SAFETY_IP={GUEST_IP}",
                    "-e", f"SAFE_EDGE_INITIAL_PEERS={GUEST_IP}:8011,{HOST_IP}:8030",
                    "safe-edge-server:fast",
                ], timeout=10, check=False)
                self._send(200, "text/plain", "OK")
            elif action == "stop_edge":
                subprocess.run(["docker", "rm", "-f", "safe-edge-edge"], timeout=5, check=False)
                self._send(200, "text/plain", "OK")
            elif action == "start_edge":
                peers = (f"{GUEST_IP}:8001,{GUEST_IP}:8002,{GUEST_IP}:8003,"
                         f"{GUEST_IP}:8011,{GUEST_IP}:8012,{GUEST_IP}:8013,{HOST_IP}:8020")
                subprocess.run(["docker", "rm", "-f", "safe-edge-edge"], timeout=5, check=False)
                subprocess.run([
                    "docker", "run", "-d", "--name", "safe-edge-edge", "--network", "host",
                    "-e", f"SAFE_EDGE_OWN_IP={HOST_IP}",
                    "-e", f"SAFE_EDGE_SAFETY_IP={GUEST_IP}",
                    "-e", f"SAFE_EDGE_NON_SAFETY_IP={GUEST_IP}",
                    "-e", f"SAFE_EDGE_INITIAL_PEERS={peers}",
                    "safe-edge-edge:fast",
                ], timeout=10, check=False)
                self._send(200, "text/plain", "OK")
            elif action == "start_load":
                with _load_lock:
                    if not _load_procs:
                        for cmd in _LOAD_CMDS:
                            _load_procs.append(
                                subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                            )
                self._send(200, "text/plain", "OK")
            elif action == "reset_latency":
                _e2e_samples.clear()
                _reaction_samples.clear()
                for key in _lat_data:
                    _lat_data[key] = {"last": None, "p50": None, "p90": None, "p95": None, "p99": None, "n": 0}
                self._send(200, "text/plain", "OK")
            elif action == "stop_load":
                with _load_lock:
                    for p in _load_procs:
                        try:
                            p.terminate()
                        except OSError:
                            pass
                    _load_procs.clear()
                self._send(200, "text/plain", "OK")
            else:
                self._send(400, "text/plain", f"Unknown action: {action}")
        except subprocess.TimeoutExpired:
            self._send(504, "text/plain", "Docker command timed out")
        except OSError as e:
            self._send(500, "text/plain", f"Error: {e}")

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()


def main():
    t = threading.Thread(target=_poll_status_file, daemon=True)
    t.start()
    threading.Thread(target=_poll_latency_logs, daemon=True).start()
    server = http.server.HTTPServer(("0.0.0.0", PORT), Handler)
    print(f"[dashboard] Listening on http://0.0.0.0:{PORT}")
    print(f"[dashboard] Status file : {STATUS_FILE} (polled at {STATUS_POLL_HZ} Hz)")
    print(f"[dashboard] Input file  : {INPUT_FILE}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[dashboard] Stopped.")


if __name__ == "__main__":
    main()
