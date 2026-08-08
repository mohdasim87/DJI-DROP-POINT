// tools/dashboard/dashboard.js
//
// Status: unverified -- app/ does not yet serve the /api/state endpoint
// this polls (see the TODO below and docs/ARCHITECTURE.md). This file
// implements the client half against a documented contract so the server
// half is a self-contained follow-on: add an HTTP listener in
// app/guidance_loop.cpp (or a thin wrapper around it) that serves the JSON
// shape polled here from whatever the last Tick() produced.
//
// No CDN, no build step, no framework: this must run with zero internet
// access on the companion computer, matching the rest of the project.

const statusEl = document.getElementById('status');
const tableBody = document.querySelector('#telemetry-table tbody');
const canvas = document.getElementById('plot');
const ctx = canvas.getContext('2d');

let connected = false;
let pollHandle = null;

function drawPlot(state) {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  const scale = 4; // px per metre, centred on the release point
  const originX = canvas.width / 2;
  const originY = canvas.height / 2;

  function toCanvas(e, n) {
    return [originX + e * scale, originY - n * scale];
  }

  ctx.strokeStyle = '#333';
  ctx.beginPath();
  ctx.moveTo(0, originY); ctx.lineTo(canvas.width, originY);
  ctx.moveTo(originX, 0); ctx.lineTo(originX, canvas.height);
  ctx.stroke();

  if (!state) return;

  if (state.dispersion_cloud) {
    ctx.fillStyle = '#888';
    for (const p of state.dispersion_cloud) {
      const [x, y] = toCanvas(p.e, p.n);
      ctx.fillRect(x - 1, y - 1, 2, 2);
    }
  }

  function marker(point, color, label) {
    if (!point) return;
    const [x, y] = toCanvas(point.e, point.n);
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(x, y, 5, 0, 2 * Math.PI);
    ctx.fill();
    ctx.fillText(label, x + 8, y);
  }

  marker(state.release_point, '#5af', 'release');
  marker(state.latched_target, '#f55', 'target');
  marker(state.predicted_impact, '#5f5', 'impact');
}

function renderTelemetry(state) {
  tableBody.innerHTML = '';
  if (!state) return;
  const rows = [
    ['mission state', state.mission_state],
    ['has solution', state.has_solution],
    ['miss distance (m)', state.miss_distance_m],
    ['time to release (s)', state.time_to_release_s],
    ['gate status', state.gate_status],
  ];
  for (const [k, v] of rows) {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${k}</td><td>${v}</td>`;
    tableBody.appendChild(tr);
  }
}

async function pollOnce() {
  try {
    // TODO(server): implement GET /api/state on the droppoint binary
    // (behind a --dashboard-port flag, bench/dev builds only, never
    // exposed on a flight-configured install) returning the JSON shape
    // consumed above.
    const response = await fetch('/api/state');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const state = await response.json();
    statusEl.textContent = 'Connected.';
    statusEl.className = 'ok';
    drawPlot(state);
    renderTelemetry(state);
  } catch (err) {
    statusEl.textContent = `Not connected (${err.message}). Dashboard server endpoint not yet implemented -- see this file's header comment.`;
    statusEl.className = 'bad';
  }
}

document.getElementById('connect-btn').addEventListener('click', () => {
  if (connected) {
    clearInterval(pollHandle);
    connected = false;
    statusEl.textContent = 'Disconnected.';
    statusEl.className = 'warn';
    return;
  }
  connected = true;
  pollHandle = setInterval(pollOnce, 500);
  pollOnce();
});

drawPlot(null);
