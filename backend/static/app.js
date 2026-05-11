/* ── Chart colors matching CSS vars ──────────────────────── */
const C = {
  navy: "#1a2e4a",
  navyFill: "rgba(26,46,74,0.1)",
  amber: "#a06800",
  amberFill: "rgba(160,104,0,0.1)",
  red: "#b52222",
  redFill: "rgba(181,34,34,0.1)",
  blue: "#2a6b9e",
  blueFill: "rgba(42,107,158,0.1)",
  border: "#c4bdb3",
  textDim: "#7a7068",
};

/* ── State ───────────────────────────────────────────────── */
let chart = null;
let chartRange = "15m";
let chartView = "occ-aq";

/* ── DOM refs ────────────────────────────────────────────── */
const $ = (id) => document.getElementById(id);

const el = {
  statusLabel: $("status-label"),
  topbarTime: $("topbar-time"),

  occupancyCount: $("occupancy-count"),
  occupancyMax: $("occupancy-max"),
  occupancyBar: $("occupancy-bar"),
  occupancyPanel: $("panel-occupancy"),
  entryCount: $("entry-count"),
  exitCount: $("exit-count"),

  airQualityVal: $("air-quality-value"),
  aqThreshold: $("aq-threshold-display"),
  aqBadge: $("aq-status-badge"),
  aqMetric: $("aq-metric"),

  tempVal: $("temp-value"),
  humidityVal: $("humidity-value"),

  fanState: $("fan-state"),
  buzzerState: $("buzzer-state"),

  eventList: $("event-list"),

  cfgMaxCap: $("cfg-max-capacity"),
  cfgAqThresh: $("cfg-aq-threshold"),
  cfgBuzzerDur: $("cfg-buzzer-duration"),
  cfgCooldown: $("cfg-cooldown"),
  cfgFeedback: $("config-feedback"),
};

/* ── Clock ───────────────────────────────────────────────── */
function updateClock() {
  el.topbarTime.textContent = new Date().toLocaleTimeString("en-GB");
}

/* ── Connection indicator ────────────────────────────────── */
function setConnected(ok) {
  el.statusLabel.textContent = ok ? "● LIVE" : "● OFFLINE";
  el.statusLabel.style.color = ok ? "var(--green)" : "var(--red)";
}

/* ── Status polling ──────────────────────────────────────── */
async function fetchStatus() {
  try {
    const r = await fetch("/status", { cache: "no-store" });
    if (!r.ok) {
      setConnected(false);
      return;
    }
    const d = await r.json();
    setConnected(true);
    applyStatus(d);
  } catch {
    setConnected(false);
  }
}

function applyStatus(d) {
  if (Date.now() / 1000 - d.timestamp > 10) {
    setConnected(false);
    return;
  }

  /* Occupancy */
  const occ = d.occupancy;
  const max = d.max_capacity;
  el.occupancyCount.textContent = occ;
  el.occupancyMax.textContent = max;
  el.entryCount.textContent = d.entry_count;
  el.exitCount.textContent = d.exit_count;

  const pct = max > 0 ? Math.min((occ / max) * 100, 100) : 0;
  el.occupancyBar.style.width = pct + "%";

  el.occupancyPanel.classList.remove("warn", "crit");
  if (occ > max) el.occupancyPanel.classList.add("crit");
  else if (occ === max) el.occupancyPanel.classList.add("warn");

  /* Air quality */
  el.airQualityVal.textContent = d.air_quality;

  const aqCrit = d.air_quality > (d.air_quality_threshold ?? Infinity);
  el.aqBadge.textContent = aqCrit ? "CRITICAL" : "NOMINAL";
  el.aqBadge.className = "status-badge" + (aqCrit ? " crit" : "");
  el.aqMetric.classList.remove("warn", "crit");
  if (aqCrit) el.aqMetric.classList.add("crit");

  /* Environment */
  el.tempVal.textContent = d.temperature.toFixed(1);
  el.humidityVal.textContent = d.humidity.toFixed(1);

  /* Fan */
  const fanLabels = {
    auto_on: ["AUTO - RUNNING", "on"],
    auto_off: ["AUTO - IDLE", ""],
    manual_on: ["MANUAL - ON", "on"],
    manual_off: ["MANUAL - OFF", ""],
  };
  const [fanText, fanCls] = fanLabels[d.fan_state] ?? ["---", ""];
  el.fanState.textContent = fanText;
  el.fanState.className = "actuator-state " + fanCls;

  /* Fan button active state */
  const fanOverride = d.fan_state.startsWith("manual_on")
    ? "on"
    : d.fan_state.startsWith("manual_off")
      ? "off"
      : "auto";
  document.querySelectorAll("[data-fan]").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.fan === fanOverride);
  });

  /* Buzzer */
  const buzzerLabels = {
    active: ["ACTIVE", "crit"],
    cooldown: ["COOLDOWN", "warn"],
    idle: ["IDLE", ""],
  };
  const [buzText, buzCls] = buzzerLabels[d.buzzer_state] ?? ["---", ""];
  el.buzzerState.textContent = buzText;
  el.buzzerState.className = "actuator-state " + buzCls;

  const silenceBtn = $("btn-silence");
  silenceBtn.disabled = d.buzzer_state !== "active";
  silenceBtn.classList.toggle("btn--disabled", d.buzzer_state !== "active");
}

/* ── Events polling ──────────────────────────────────────── */
async function fetchEvents() {
  try {
    const r = await fetch("/events?limit=30");
    if (!r.ok) return;
    const events = await r.json();
    renderEvents(events);
  } catch {
    /* silent */
  }
}

function renderEvents(events) {
  if (!events.length) return;
  el.eventList.innerHTML = events
    .map((ev) => {
      const t = new Date(ev.timestamp * 1000).toLocaleTimeString("en-GB");
      const cls = ev.event_type === "entry" ? "entry" : "exit";
      const lbl = ev.event_type.toUpperCase();
      return `<li class="event-item ${cls}">
            <span class="event-time">${t}</span>
            <span class="event-type">${lbl}</span>
            <span class="event-occ">occ: ${ev.occupancy_after}</span>
        </li>`;
    })
    .join("");
}

/* ── History chart ───────────────────────────────────────── */
async function fetchHistory() {
  try {
    const r = await fetch(`/history?range=${chartRange}`);
    if (!r.ok) return;
    const rows = await r.json();
    renderChart(rows);
  } catch {
    /* silent */
  }
}

function renderChart(rows) {
  const labels = rows.map((r) =>
    new Date(r.timestamp * 1000).toLocaleTimeString("en-GB", {
      hour: "2-digit",
      minute: "2-digit",
    }),
  );

  let datasets, yLeft, yRight, legendHtml;

  if (chartView === "occ-aq") {
    datasets = [
      {
        label: "Occupancy",
        data: rows.map((r) => r.occupancy),
        borderColor: C.navy,
        backgroundColor: C.navyFill,
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.3,
        fill: true,
        yAxisID: "y",
      },
      {
        label: "Air Quality (PPM)",
        data: rows.map((r) => r.air_quality),
        borderColor: C.amber,
        backgroundColor: C.amberFill,
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.3,
        fill: true,
        yAxisID: "y1",
      },
    ];
    yLeft = {
      position: "left",
      title: {
        display: true,
        text: "OCC",
        color: C.navy,
        font: { family: "JetBrains Mono", size: 8 },
      },
      ticks: { color: C.navy, font: { family: "JetBrains Mono", size: 9 } },
      grid: { color: C.border },
      border: { color: C.navy },
      beginAtZero: true,
    };
    yRight = {
      position: "right",
      title: {
        display: true,
        text: "ADC",
        color: C.amber,
        font: { family: "JetBrains Mono", size: 8 },
      },
      ticks: { color: C.amber, font: { family: "JetBrains Mono", size: 9 } },
      grid: { drawOnChartArea: false },
      border: { color: C.amber },
      beginAtZero: false,
    };
    legendHtml = `<span class="legend-line legend-line--occ"></span>OCCUPANCY (left) <span class="legend-line legend-line--aq"></span>AIR QUALITY ADC (right)`;
  } else {
    datasets = [
      {
        label: "Temperature (°C)",
        data: rows.map((r) => r.temperature),
        borderColor: C.red,
        backgroundColor: C.redFill,
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.3,
        fill: true,
        yAxisID: "y",
      },
      {
        label: "Humidity (%RH)",
        data: rows.map((r) => r.humidity),
        borderColor: C.blue,
        backgroundColor: C.blueFill,
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.3,
        fill: true,
        yAxisID: "y1",
      },
    ];
    yLeft = {
      position: "left",
      title: {
        display: true,
        text: "°C",
        color: C.red,
        font: { family: "JetBrains Mono", size: 8 },
      },
      ticks: { color: C.red, font: { family: "JetBrains Mono", size: 9 } },
      grid: { color: C.border },
      border: { color: C.red },
      beginAtZero: false,
    };
    yRight = {
      position: "right",
      title: {
        display: true,
        text: "%RH",
        color: C.blue,
        font: { family: "JetBrains Mono", size: 8 },
      },
      ticks: { color: C.blue, font: { family: "JetBrains Mono", size: 9 } },
      grid: { drawOnChartArea: false },
      border: { color: C.blue },
      beginAtZero: false,
    };
    legendHtml = `<span class="legend-line" style="background:${C.red}"></span>TEMPERATURE °C (left) <span class="legend-line" style="background:${C.blue}"></span>HUMIDITY %RH (right)`;
  }

  document.getElementById("chart-legend").innerHTML = legendHtml;

  const data = { labels, datasets };
  const options = {
    responsive: true,
    maintainAspectRatio: false,
    animation: false,
    plugins: {
      legend: { display: false },
      tooltip: {
        backgroundColor: "#f5f2ec",
        borderColor: C.border,
        borderWidth: 1,
        titleColor: C.textDim,
        bodyColor: "#0f0f0f",
        titleFont: { family: "JetBrains Mono", size: 10 },
        bodyFont: { family: "JetBrains Mono", size: 11 },
      },
    },
    scales: {
      x: {
        ticks: { color: C.textDim, font: { family: "JetBrains Mono", size: 9 }, maxTicksLimit: 8 },
        grid: { color: C.border },
        border: { color: C.border },
      },
      y: yLeft,
      y1: yRight,
    },
  };

  if (chart) {
    chart.data = data;
    chart.options = options;
    chart.update("none");
  } else {
    chart = new Chart($("history-chart"), { type: "line", data, options });
  }
}

/* ── Config load + apply ─────────────────────────────────── */
async function fetchConfig() {
  try {
    const r = await fetch("/config");
    if (!r.ok) return;
    const cfg = await r.json();
    el.cfgMaxCap.value = cfg.max_capacity;
    el.cfgAqThresh.value = cfg.air_quality_threshold;
    el.cfgBuzzerDur.value = cfg.buzzer_duration_s;
    el.cfgCooldown.value = cfg.cooldown_duration_s;
    el.aqThreshold.textContent = cfg.air_quality_threshold;
  } catch {
    /* silent */
  }
}

async function applyConfig() {
  const payload = {
    max_capacity: parseInt(el.cfgMaxCap.value),
    air_quality_threshold: parseInt(el.cfgAqThresh.value),
    buzzer_duration_s: parseInt(el.cfgBuzzerDur.value),
    cooldown_duration_s: parseInt(el.cfgCooldown.value),
  };

  const limits = {
    max_capacity: { min: 1, max: 50 },
    air_quality_threshold: { min: 1, max: 4095 },
    buzzer_duration_s: { min: 1, max: 60 },
    cooldown_duration_s: { min: 5, max: 300 },
  };

  for (const [k, v] of Object.entries(payload)) {
    const { min, max } = limits[k];
    if (isNaN(v) || v < min || v > max) {
      showFeedback(`${k.toUpperCase().replace(/_/g, " ")}: ${min}–${max}`, "err");
      return;
    }
  }

  try {
    const r = await fetch("/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    if (r.ok) {
      showFeedback("APPLIED", "ok");
      el.aqThreshold.textContent = payload.air_quality_threshold;
    } else {
      const err = await r.json();
      showFeedback(err.detail ?? "ERROR", "err");
    }
  } catch {
    showFeedback("REQUEST FAILED", "err");
  }
}

function showFeedback(msg, cls) {
  el.cfgFeedback.textContent = msg;
  el.cfgFeedback.className = "config-feedback " + cls;
  setTimeout(() => {
    el.cfgFeedback.textContent = "";
    el.cfgFeedback.className = "config-feedback";
  }, 3000);
}

/* ── Fan control ─────────────────────────────────────────── */
async function setFan(state) {
  try {
    await fetch("/fan", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ state }),
    });
  } catch {
    /* silent */
  }
}

/* ── Buzzer silence ──────────────────────────────────────── */
async function silenceBuzzer() {
  try {
    await fetch("/buzzer/silence", { method: "POST" });
  } catch {
    /* silent */
  }
}

/* ── Event listeners ─────────────────────────────────────── */
document.querySelectorAll("[data-fan]").forEach((btn) => {
  btn.addEventListener("click", () => setFan(btn.dataset.fan));
});

document.querySelectorAll("[data-view]").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll("[data-view]").forEach((b) => b.classList.remove("active"));
    btn.classList.add("active");
    chartView = btn.dataset.view;
    fetchHistory();
  });
});

document.querySelectorAll("[data-range]").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll("[data-range]").forEach((b) => b.classList.remove("active"));
    btn.classList.add("active");
    chartRange = btn.dataset.range;
    fetchHistory();
  });
});

$("btn-silence").addEventListener("click", silenceBuzzer);
$("btn-apply-config").addEventListener("click", applyConfig);

/* ── Init + polling loops ────────────────────────────────── */
setInterval(updateClock, 1000);
updateClock();

fetchConfig();
fetchStatus();
fetchHistory();
fetchEvents();

setInterval(fetchStatus, 2000);
setInterval(fetchEvents, 3000);
setInterval(fetchHistory, 5000);
