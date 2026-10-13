/*
 * EmbedIDS EdgeML dashboard - dependency-free, polls the agent REST API.
 * Works behind the nginx proxy (/api -> agent) and when served by the agent.
 */
(function () {
  "use strict";

  const API = "api";
  const POLL_MS = 1000;
  const HISTORY_SECONDS = 300;

  const state = {
    status: null,
    alerts: [],
    selectedAlert: null,
    seenAlerts: new Set(),
    firstLoad: true,
    activeOnly: false,
    busy: false,
  };

  // ---------------------------------------------------------------- helpers
  const $ = (id) => document.getElementById(id);

  function esc(value) {
    return String(value === undefined || value === null ? "" : value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function fmtTime(iso) {
    if (!iso) return "";
    const d = new Date(iso);
    return d.toLocaleTimeString([], { hour12: false });
  }

  function fmtNum(value, digits) {
    if (value === null || value === undefined || Number.isNaN(value)) return "–";
    return Number(value).toFixed(digits === undefined ? 1 : digits);
  }

  function fmtRate(bytes) {
    if (bytes === null || bytes === undefined) return "–";
    if (bytes >= 1e6) return (bytes / 1e6).toFixed(2) + " MB/s";
    return (bytes / 1e3).toFixed(1) + " KB/s";
  }

  function shortHash(hash) {
    if (!hash) return "–";
    return hash.length > 16 ? hash.slice(0, 12) + "…" : hash;
  }

  async function getJson(path) {
    const res = await fetch(`${API}/${path}`, { cache: "no-store" });
    if (!res.ok) throw new Error(`${path}: HTTP ${res.status}`);
    return res.json();
  }

  async function postJson(path) {
    const res = await fetch(`${API}/${path}`, { method: "POST" });
    const body = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(body.error || `HTTP ${res.status}`);
    return body;
  }

  // ----------------------------------------------------------------- charts
  const CHARTS = [
    { metric: "system.cpu.utilization", title: "CPU utilization", unit: "%", min: 0, max: 100,
      color: "#4ea1ff", thresholds: (t) => [{ v: t.cpu_exhaustion, label: "exhaustion" }] },
    { metric: "gpu.utilization", title: "GPU utilization", unit: "%", min: 0, max: 100,
      color: "#76b900", thresholds: (t) => [{ v: t.gpu_high_utilization, label: "high" }] },
    { metric: "gpu.memory.percent", title: "GPU memory", unit: "%", min: 0, max: 100,
      color: "#a78bfa", thresholds: () => [] },
    { metric: "ai.inference.rate", title: "Inference rate", unit: "FPS", min: 0, max: 40,
      color: "#3ccf7a", low: true,
      thresholds: (t) => [{ v: t.minimum_fps, label: "minimum" }, { v: t.baseline_fps, label: "baseline", soft: true }] },
    { metric: "ai.inference.latency", title: "Inference latency", unit: "ms", min: 0, maxAuto: 60,
      color: "#f5b83d", thresholds: () => [] },
    { metric: "network.connections.outbound", title: "Outbound connections", unit: "", min: 0, maxAuto: 5,
      color: "#22d3ee", digits: 0, thresholds: () => [] },
    { metric: "network.bytes.out", title: "Outbound traffic", unit: "", min: 0, maxAuto: 250000,
      color: "#ff6b4a", rate: true, thresholds: (t) => [{ v: t.bytes_out_anomaly, label: "anomaly" }] },
    { metric: "system.process.count", title: "Process count", unit: "", minAuto: true, maxAuto: 200,
      color: "#cbd5e1", digits: 0, thresholds: () => [] },
  ];

  function buildCharts() {
    const container = $("charts");
    container.innerHTML = CHARTS.map((c, i) => `
      <div class="chart">
        <div class="chart-head"><span>${esc(c.title)}</span>
          <span class="chart-value" id="chart-value-${i}">–</span></div>
        <canvas id="chart-${i}"></canvas>
      </div>`).join("");
  }

  function drawChart(canvas, points, cfg, thresholds, nowMs) {
    const dpr = window.devicePixelRatio || 1;
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (canvas.width !== width * dpr || canvas.height !== height * dpr) {
      canvas.width = width * dpr;
      canvas.height = height * dpr;
    }
    const ctx = canvas.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, height);

    const t0 = nowMs - HISTORY_SECONDS * 1000;
    const values = points.map((p) => p[1]);
    let lo = cfg.min !== undefined ? cfg.min : 0;
    let hi = cfg.max !== undefined ? cfg.max : cfg.maxAuto || 1;
    if (cfg.maxAuto !== undefined) {
      const peak = Math.max(cfg.maxAuto, ...values, ...thresholds.map((t) => t.v * 1.1));
      hi = peak * 1.05;
    }
    if (cfg.minAuto && values.length) {
      lo = Math.max(0, Math.min(...values) * 0.9);
      hi = Math.max(...values) * 1.1;
    }
    if (hi <= lo) hi = lo + 1;
    const x = (t) => ((t - t0) / (HISTORY_SECONDS * 1000)) * width;
    const y = (v) => height - 4 - ((v - lo) / (hi - lo)) * (height - 8);

    // grid
    ctx.strokeStyle = "rgba(139,151,179,0.12)";
    ctx.lineWidth = 1;
    for (let i = 1; i < 4; i++) {
      const gy = (height / 4) * i;
      ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(width, gy); ctx.stroke();
    }

    // thresholds
    thresholds.forEach((t) => {
      if (t.v === undefined || t.v === null || t.v < lo || t.v > hi) return;
      ctx.setLineDash([4, 4]);
      ctx.strokeStyle = t.soft ? "rgba(245,184,61,0.45)" : "rgba(255,107,74,0.8)";
      ctx.beginPath(); ctx.moveTo(0, y(t.v)); ctx.lineTo(width, y(t.v)); ctx.stroke();
      ctx.setLineDash([]);
    });

    if (points.length < 2) return;

    // area + line
    ctx.beginPath();
    points.forEach((p, i) => (i ? ctx.lineTo(x(p[0]), y(p[1])) : ctx.moveTo(x(p[0]), y(p[1]))));
    ctx.strokeStyle = cfg.color;
    ctx.lineWidth = 1.8;
    ctx.stroke();
    ctx.lineTo(x(points[points.length - 1][0]), height);
    ctx.lineTo(x(points[0][0]), height);
    ctx.closePath();
    ctx.fillStyle = cfg.color + "22";
    ctx.fill();
  }

  function renderCharts(metrics) {
    const thresholdsCfg = (state.status && state.status.thresholds) || {};
    const nowMs = Date.parse(metrics.timestamp) || Date.now();
    CHARTS.forEach((cfg, i) => {
      const points = (metrics.history && metrics.history[cfg.metric]) || [];
      const latest = metrics.latest && metrics.latest[cfg.metric];
      const thresholds = cfg.thresholds(thresholdsCfg);
      const valueEl = $(`chart-value-${i}`);
      if (latest) {
        const v = latest.value;
        valueEl.textContent = cfg.rate ? fmtRate(v) : `${fmtNum(v, cfg.digits)}${cfg.unit ? " " + cfg.unit : ""}`;
        const hard = thresholds.filter((t) => !t.soft && t.v !== undefined);
        const breach = hard.some((t) => (cfg.low ? v < t.v : v > t.v));
        valueEl.classList.toggle("breach", breach);
      } else {
        valueEl.textContent = "–";
      }
      drawChart($(`chart-${i}`), points, cfg, thresholds, nowMs);
    });
  }

  // --------------------------------------------------------------- overview
  function renderStatus(status, metrics) {
    const latest = (metrics && metrics.latest) || {};
    $("device-line").textContent =
      `${status.device_id} · ${status.platform} platform · EmbedIDS core ${status.agent.embedids_core}`;

    const stateName = status.security_state;
    const stateEl = $("security-state");
    stateEl.textContent = stateName;
    stateEl.className = `state-value state-${stateName}`;
    $("state-card").className = `card state-card state-bg-${stateName}`;
    $("state-foot").textContent = stateName === "HEALTHY"
      ? "no active detections" : `${status.active_alerts} active alert(s)`;

    const fps = latest["ai.inference.rate"];
    $("workload").textContent = fps ? `${fmtNum(fps.value)} FPS` : "–";
    const gpu = latest["gpu.utilization"];
    $("workload-foot").textContent =
      `${status.workload}${gpu ? " · GPU " + fmtNum(gpu.value, 0) + "%" : ""}`;

    $("active-alerts").textContent = status.active_alerts;
    const sev = status.active_by_severity || {};
    $("sev-counts").innerHTML = ["critical", "high", "medium"]
      .map((s) => `<span class="sev sev-${s}">${sev[s] || 0} ${s}</span>`).join("");
    $("detections").textContent = status.detections_total;

    const integrity = status.model_integrity || {};
    const integrityStatus = (integrity.status || "unknown").toUpperCase();
    $("integrity").textContent = integrityStatus;
    $("integrity-card").className = `card integrity-card ${integrity.status || ""}`;
    $("integrity-foot").textContent = integrity.current
      ? `sha256 ${shortHash(integrity.current)}` : (integrity.path || "");

    const agent = status.agent || {};
    $("agent-cpu").textContent = agent.cpu_percent !== null && agent.cpu_percent !== undefined
      ? `${fmtNum(agent.cpu_percent)}% CPU` : "–";
    $("agent-foot").textContent =
      `${fmtNum(agent.memory_rss_mb)} MB RSS · ${fmtNum(agent.processing_latency_ms, 1)} ms/cycle`;
    $("footer-agent").textContent =
      `agent ${agent.version} · binary ${shortHash(agent.binary_sha256)} · config ${shortHash(agent.config_sha256)}`;

    const age = status.heartbeat_age_seconds;
    const timeout = (status.thresholds || {}).heartbeat_timeout_seconds || 15;
    const lost = age !== null && age > timeout;
    $("heartbeat").classList.toggle("lost", lost);
    $("heartbeat-text").textContent = age === null || age === undefined
      ? "heartbeat –" : lost ? `telemetry lost (${fmtNum(age, 0)} s)` : `heartbeat ${fmtNum(age, 0)} s`;
    $("updated").textContent = `updated ${fmtTime(status.timestamp)}`;

    // Demo controls
    const demo = status.demo_controls;
    $("demo-panel").classList.toggle("hidden", !demo);
    const sim = status.simulation || {};
    const active = new Set(sim.active_attacks || []);
    document.querySelectorAll("[data-scenario]").forEach((btn) => {
      btn.classList.toggle("active", active.has(btn.dataset.scenario));
    });
    $("demo-note").textContent = sim.reachable === false
      ? "Simulator unreachable"
      : active.size ? `Active: ${Array.from(active).join(", ")}` : "Simulated attacks against the EdgeML device";
  }

  // ----------------------------------------------------------------- alerts
  function evidenceSummary(alert) {
    const ev = alert.evidence || {};
    const observedKeys = ["gpu_utilization", "inference_rate", "bytes_out_per_s", "cpu_utilization",
      "memory_percent", "process", "destination", "heartbeat_age_seconds", "current_sha256", "readers"];
    const thresholdKeys = ["gpu_threshold", "expected_fps", "minimum_fps", "escalation_threshold",
      "baseline_bytes_out", "threshold_bytes_out", "timeout_seconds", "expected_sha256"];
    const fmt = (k, v) => {
      if (Array.isArray(v)) v = v.join(", ");
      if (k.endsWith("sha256")) v = shortHash(v);
      else if (k.indexOf("bytes") >= 0 || k.indexOf("threshold_bytes") >= 0) v = fmtRate(v);
      else if (typeof v === "number") v = fmtNum(v);
      return `<span class="chip">${esc(k.replace(/_/g, " "))}: ${esc(v)}</span>`;
    };
    const observed = observedKeys.filter((k) => ev[k] !== undefined && ev[k] !== "").map((k) => fmt(k, ev[k]));
    const thresholds = thresholdKeys.filter((k) => ev[k] !== undefined).map((k) => fmt(k, ev[k]));
    return { observed: observed.join("") || "–", thresholds: thresholds.join("") || "–" };
  }

  function renderAlerts(alerts) {
    const body = $("alerts-body");
    const list = state.activeOnly ? alerts.filter((a) => a.status === "active") : alerts;
    if (!list.length) {
      body.innerHTML = `<tr><td colspan="7" class="empty">No alerts${state.activeOnly ? " active" : " yet"}</td></tr>`;
      return;
    }
    body.innerHTML = list.map((a) => {
      const summary = evidenceSummary(a);
      const correlated = a.category === "correlated_incident"
        ? '<span class="correlated-tag">correlated</span>' : "";
      const subject = a.subject ? `<div class="alert-sub">${esc(a.subject)}</div>` : "";
      return `<tr class="alert-row ${a.status === "resolved" ? "resolved" : ""} ${a.alert_id === state.selectedAlert ? "selected" : ""}" data-id="${esc(a.alert_id)}">
        <td class="mono">${esc(fmtTime(a.timestamp))}</td>
        <td><span class="sev sev-${esc(a.severity)}">${esc(a.severity)}</span></td>
        <td><div class="alert-title">${esc(a.title)}${correlated}</div>
            <div class="alert-sub">${esc(a.alert_id)} · ${esc(a.rule)}</div>${subject}</td>
        <td><div class="chips">${summary.observed}</div></td>
        <td><div class="chips">${summary.thresholds}</div></td>
        <td><div class="chips">${(a.signals || []).map((s) => `<span class="chip">${esc(s)}</span>`).join("")}</div></td>
        <td>${a.status === "active" ? '<span class="sev sev-high">active</span>' : '<span class="muted">resolved</span>'}</td>
      </tr>`;
    }).join("");
  }

  function renderEvidence(alert) {
    const el = $("evidence");
    if (!alert) {
      el.className = "evidence empty";
      el.textContent = "Select an alert to see why it was flagged.";
      return;
    }
    el.className = "evidence";
    const ev = alert.evidence || {};
    const rows = Object.keys(ev)
      .filter((k) => k !== "correlated_alerts")
      .map((k) => {
        let v = ev[k];
        if (typeof v === "object") v = JSON.stringify(v);
        return `<dt>${esc(k)}</dt><dd>${esc(v)}</dd>`;
      }).join("");
    let hashes = "";
    if (ev.expected_sha256 && ev.current_sha256) {
      hashes = `<div class="hash-compare">
        <div class="hash-expected">EXPECTED<br>${esc(ev.expected_sha256)}</div>
        <div class="hash-current">CURRENT<br>${esc(ev.current_sha256)}</div></div>`;
    }
    let related = "";
    if (Array.isArray(ev.correlated_alerts) && ev.correlated_alerts.length) {
      related = `<div class="related"><strong>Correlated signals</strong><ul>${ev.correlated_alerts.map((c) =>
        `<li><span class="sev sev-${esc(c.severity)}">${esc(c.severity)}</span> ${esc(c.title)} <span class="muted">${esc(c.alert_id)}</span></li>`
      ).join("")}</ul></div>`;
    }
    el.innerHTML = `
      <h3><span class="sev sev-${esc(alert.severity)}">${esc(alert.severity)}</span> ${esc(alert.title)}</h3>
      <div class="muted mono">${esc(alert.alert_id)} · ${esc(alert.rule)} · ${esc(alert.category)} · ${esc(fmtTime(alert.timestamp))}</div>
      <div class="desc">${esc(alert.description)}</div>
      ${hashes}
      <dl>${rows}</dl>
      ${related}
      ${alert.resolved ? `<div class="muted resolved-note">Resolved ${esc(fmtTime(alert.resolved))} (${esc(alert.resolution)})</div>` : ""}`;
  }

  function announceNewAlerts(alerts) {
    const fresh = alerts.filter((a) => !state.seenAlerts.has(a.alert_id));
    alerts.forEach((a) => state.seenAlerts.add(a.alert_id));
    if (state.firstLoad || !fresh.length) return;
    const order = { critical: 4, high: 3, medium: 2, low: 1, info: 0 };
    const top = fresh.sort((a, b) => order[b.severity] - order[a.severity])[0];
    const banner = $("banner");
    const colors = { critical: "#e0398f", high: "#ff6b4a", medium: "#f5b83d", low: "#4ea1ff", info: "#4ea1ff" };
    banner.style.borderColor = colors[top.severity];
    banner.style.background = colors[top.severity] + "22";
    banner.innerHTML = `[${esc(top.severity.toUpperCase())}] ${esc(top.title)} — ${esc(top.description)}`;
    banner.classList.remove("hidden");
    clearTimeout(announceNewAlerts.timer);
    announceNewAlerts.timer = setTimeout(() => banner.classList.add("hidden"), 8000);
  }

  // --------------------------------------------------------------- timeline
  function renderTimeline(events) {
    $("timeline").innerHTML = events.map((e) => `
      <li><span class="t">${esc(fmtTime(e.timestamp))}</span>
          <span class="type type-${esc(e.type.replace(/\./g, "-"))}">${esc(e.type)}</span>
          <span>${esc(e.message)}</span></li>`).join("");
  }

  // ------------------------------------------------------------------- poll
  async function refresh() {
    try {
      const [status, metrics, alerts, events] = await Promise.all([
        getJson("status"), getJson(`metrics?history=${HISTORY_SECONDS}`),
        getJson("alerts?limit=100"), getJson("events?limit=60"),
      ]);
      state.status = status;
      state.alerts = alerts;
      renderStatus(status, metrics);
      renderCharts(metrics);
      announceNewAlerts(alerts);
      renderAlerts(alerts);
      renderTimeline(events);
      if (state.selectedAlert) {
        renderEvidence(alerts.find((a) => a.alert_id === state.selectedAlert));
      }
      state.firstLoad = false;
    } catch (err) {
      $("updated").textContent = `agent unreachable (${err.message})`;
      $("heartbeat").classList.add("lost");
      $("heartbeat-text").textContent = "agent offline";
    }
  }

  async function trigger(btn) {
    if (state.busy) return;
    state.busy = true;
    document.querySelectorAll(".demo-buttons .btn").forEach((b) => (b.disabled = true));
    try {
      if (btn.dataset.action === "reset") {
        await postJson("demo/reset");
        state.selectedAlert = null;
        renderEvidence(null);
      } else {
        await postJson(`demo/scenario/${btn.dataset.scenario}`);
      }
      await refresh();
    } catch (err) {
      alert(`Demo action failed: ${err.message}`);
    } finally {
      state.busy = false;
      document.querySelectorAll(".demo-buttons .btn").forEach((b) => (b.disabled = false));
    }
  }

  function init() {
    buildCharts();
    document.querySelectorAll(".demo-buttons .btn").forEach((btn) =>
      btn.addEventListener("click", () => trigger(btn)));
    $("alerts-body").addEventListener("click", (event) => {
      const row = event.target.closest("tr[data-id]");
      if (!row) return;
      state.selectedAlert = row.dataset.id;
      renderAlerts(state.alerts);
      renderEvidence(state.alerts.find((a) => a.alert_id === state.selectedAlert));
    });
    $("active-only").addEventListener("change", (event) => {
      state.activeOnly = event.target.checked;
      renderAlerts(state.alerts);
    });
    window.addEventListener("resize", () => refresh());
    refresh();
    setInterval(refresh, POLL_MS);
  }

  document.addEventListener("DOMContentLoaded", init);
})();
