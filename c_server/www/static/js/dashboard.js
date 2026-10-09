// Global references for Charts
let tempChart = null;
let voltageChart = null;
let currentChart = null;

// Simulation states
let simulationInterval = null;
let activeFaultMode = "none";

// Initialize when document loads
document.addEventListener("DOMContentLoaded", () => {
    initCharts();
    setInterval(pollSensorData, 1000);
    runSimulationTick();
});

function initCharts() {
    const opts = {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        scales: {
            y: { grid: { color: "rgba(255,255,255,0.03)" }, ticks: { color: "#64748b", font: { size: 9 } } },
            x: { grid: { display: false }, ticks: { color: "#64748b", font: { size: 9 } } }
        },
        plugins: { legend: { display: false } }
    };
    tempChart = new Chart(document.getElementById("tempChart").getContext("2d"), {
        type: "line", data: { labels: [], datasets: [{ label: "Temp", data: [], borderColor: "#00f2fe", backgroundColor: "rgba(0,242,254,0.05)", borderWidth: 1.5, tension: 0.2, fill: true, pointRadius: 1 }] }, options: opts
    });
    voltageChart = new Chart(document.getElementById("voltageChart").getContext("2d"), {
        type: "line", data: { labels: [], datasets: [{ label: "Voltage", data: [], borderColor: "#10b981", backgroundColor: "rgba(16,185,129,0.05)", borderWidth: 1.5, tension: 0.2, fill: true, pointRadius: 1 }] }, options: opts
    });
    currentChart = new Chart(document.getElementById("currentChart").getContext("2d"), {
        type: "line", data: { labels: [], datasets: [{ label: "Current", data: [], borderColor: "#f59e0b", backgroundColor: "rgba(245,158,11,0.05)", borderWidth: 1.5, tension: 0.2, fill: true, pointRadius: 1 }] }, options: opts
    });
}

async function pollSensorData() {
    try {
        const r = await fetch("/get_sensor_data");
        if (!r.ok) return;
        const data = await r.json();
        updateSystemStatus(data);
        updateTelemetryMetrics(data.telemetry, data.health);
        updateActuators(data.commands, data.telemetry);
        updateSelfHealingChecklist(data.telemetry);
        updateLogsList(data.logs);
        updateTrendsCharts(data.chart_data);
        updateAlertsBanner(data.alerts);
    } catch (e) { console.error("Poll error:", e); }
}

function setText(id, val) { const el = document.getElementById(id); if (el) el.innerText = val; }

function updateSystemStatus(data) {
    const hasAlerts = data.alerts && data.alerts.length > 0;
    const hasCritical = hasAlerts && data.alerts.some(a => a.severity === "CRITICAL");
    const statusText = document.getElementById("status-text");
    const statusPulse = document.getElementById("status-pulse");
    const statusDesc = document.getElementById("status-desc");
    if (hasCritical) {
        if (statusText) { statusText.innerText = "CRITICAL FAULT"; statusText.className = "text-red-500"; }
        if (statusPulse) statusPulse.className = "pulse-dot inline-block h-3 w-3 rounded-full bg-red-500";
        if (statusDesc) statusDesc.innerText = "Critical safety condition detected. Self-healing protocols engaged.";
    } else if (hasAlerts) {
        if (statusText) { statusText.innerText = "Warning"; statusText.className = "text-amber-400"; }
        if (statusPulse) statusPulse.className = "pulse-dot inline-block h-3 w-3 rounded-full bg-amber-500";
        if (statusDesc) statusDesc.innerText = "Parameter threshold exceeded. Monitoring elevated.";
    } else {
        if (statusText) { statusText.innerText = "Nominal"; statusText.className = "text-primary"; }
        if (statusPulse) statusPulse.className = "pulse-dot inline-block h-3 w-3 rounded-full bg-primary";
        if (statusDesc) statusDesc.innerText = "Digital twin reconciling sensor stream from ESP32 every second.";
    }
    if (data.health) {
        setText("meta-cycles", data.health.estimated_cycles || "--");
        setText("meta-life", data.health.estimated_remaining_life ? data.health.estimated_remaining_life + " cyc" : "--");
    }
}

function updateTelemetryMetrics(telemetry, health) {
    if (!telemetry || !health) return;
    setText("metric-temp", telemetry.temperature.toFixed(1));
    setText("metric-voltage", telemetry.voltage.toFixed(2));
    setText("metric-current", telemetry.current.toFixed(2));
    setText("metric-soc", telemetry.battery_percentage.toFixed(1));
    setText("metric-soh", health.health_score.toFixed(1));
    setText("metric-health", Math.round(health.health_score));
    setText("txt-soc", telemetry.battery_percentage.toFixed(1) + " %");
    setText("txt-soh", health.health_score.toFixed(1) + " %");
    setText("txt-voltage", telemetry.voltage.toFixed(2) + " V");
    setText("txt-current", telemetry.current.toFixed(2) + " A");
    setText("txt-temp", telemetry.temperature.toFixed(1) + " C");
    const soc = Math.max(0, Math.min(100, telemetry.battery_percentage));
    const fillBar = document.getElementById("battery-fill-bar");
    const fillPct = document.getElementById("battery-fill-percent");
    if (fillBar) fillBar.style.height = soc + "%";
    if (fillPct) fillPct.innerText = Math.round(soc) + "%";
    const box = document.getElementById("twin-battery-box");
    if (box) box.className = "relative flex h-44 w-24 flex-col-reverse overflow-hidden rounded-xl border-2 border-border bg-black/40 battery-simulator" + (telemetry.temperature > 55 ? " critical-alert" : "");
}

function updateAlertsBanner(alerts) {
    const c = document.getElementById("alerts-container");
    if (!c) return;
    if (!alerts || alerts.length === 0) { c.innerHTML = ""; return; }
    c.innerHTML = alerts.map(a => {
        const isCrit = a.severity === "CRITICAL";
        const col = isCrit ? "red" : "amber";
        return `<div class="flex items-start gap-3 rounded-lg p-3 border mb-2 bg-${col}-500/10 border-${col}-500/30 text-${col}-400 text-sm">
            <i class="fa-solid fa-triangle-exclamation mt-0.5"></i>
            <div><b class="text-xs uppercase tracking-wide">${a.severity} - ${a.alert_type}</b><br><span class="text-xs text-gray-300">${a.message}</span></div>
        </div>`;
    }).join("");
}

function updateActuators(commands, telemetry) {
    if (!commands) return;

    const sliderTempEl = document.getElementById("slide-temp");
    const sliderLoadEl = document.getElementById("slide-load");
    const sliderTemp = sliderTempEl ? parseFloat(sliderTempEl.value) : 28;
    const sliderLoad = sliderLoadEl ? parseFloat(sliderLoadEl.value) : 0.4;
    const sliderCalcTemp = sliderTemp + Math.pow(sliderLoad * 4.5, 2) * 1.25;
    const rawTemp = (telemetry && telemetry.temperature !== undefined) ? telemetry.temperature : sliderCalcTemp;

    // -- FAN ------------------------------------------------------------------
    const fanVal  = document.getElementById("val-actuator-fan");
    const fanCard = document.getElementById("card-actuator-fan");
    const fanIcon = document.getElementById("icon-actuator-fan");

    let fanSpeedText = "OFF";
    let fanSpeedDuration = "2s";
    let isSpinning = false;

    const effectiveTemp = activeFaultMode === "overheat" ? Math.max(rawTemp, 58.5) : rawTemp;
    const fanForced = commands.fan || activeFaultMode === "overheat";

    if (commands.fan_mode === "off") {
        fanSpeedText = "OFF"; isSpinning = false;
    } else if (commands.fan_mode === "on") {
        fanSpeedText = "MANUAL ON"; fanSpeedDuration = "0.4s"; isSpinning = true;
    } else {
        // auto mode - use temperature
        if (fanForced || effectiveTemp >= 35) {
            if (effectiveTemp >= 55) { fanSpeedText = "MAX (100%)"; fanSpeedDuration = "0.22s"; isSpinning = true; }
            else if (effectiveTemp >= 45) { fanSpeedText = "HIGH (80%)"; fanSpeedDuration = "0.45s"; isSpinning = true; }
            else if (effectiveTemp >= 40) { fanSpeedText = "MED (55%)"; fanSpeedDuration = "0.85s"; isSpinning = true; }
            else if (effectiveTemp >= 35) { fanSpeedText = "LOW (30%)"; fanSpeedDuration = "1.5s"; isSpinning = true; }
            else { fanSpeedText = "OFF"; isSpinning = false; }
        }
    }

    if (fanVal) fanVal.innerText = fanSpeedText;
    if (isSpinning) {
        if (fanCard) fanCard.className = "flex items-center gap-3 rounded-lg p-3 ring-1 bg-primary/20 text-primary ring-primary/40 shadow-[0_0_15px_rgba(0,242,254,0.15)]";
        if (fanIcon) { fanIcon.style.setProperty("--fan-speed", fanSpeedDuration); fanIcon.className = "fa-solid fa-fan text-sm text-primary spin-fan-active"; }
    } else {
        if (fanCard) fanCard.className = "flex items-center gap-3 rounded-lg p-3 ring-1 bg-black/10 text-gray-300 ring-border";
        if (fanIcon) { fanIcon.style.removeProperty("--fan-speed"); fanIcon.className = "fa-solid fa-fan text-sm text-gray-500"; }
    }

    // -- RELAY ----------------------------------------------------------------
    const relayActive = commands.relay;
    const relayVal  = document.getElementById("val-actuator-relay");
    const relayCard = document.getElementById("card-actuator-relay");
    const relayIcon = document.getElementById("icon-actuator-relay");
    if (relayVal) relayVal.innerText = relayActive ? "ON" : "OFF (TRIPPED)";
    if (relayActive) {
        if (relayCard) relayCard.className = "flex items-center gap-3 rounded-lg p-3 ring-1 bg-primary/20 text-primary ring-primary/40";
        if (relayIcon) relayIcon.className = "fa-solid fa-bolt text-sm text-primary";
    } else {
        if (relayCard) relayCard.className = "flex items-center gap-3 rounded-lg p-3 ring-1 relay-tripped-card text-red-400";
        if (relayIcon) relayIcon.className = "fa-solid fa-bolt-slash text-sm text-red-500 animate-pulse";
    }
    setText("txt-load", relayActive ? "Connected" : "ISOLATED");

    // -- BUZZER ---------------------------------------------------------------
    const buzzerActive = commands.buzzer;
    const buzzerVal  = document.getElementById("val-actuator-buzzer");
    const buzzerCard = document.getElementById("card-actuator-buzzer");
    const buzzerIcon = document.getElementById("icon-actuator-buzzer");
    if (buzzerVal) buzzerVal.innerText = buzzerActive ? "ON" : "OFF";
    if (buzzerActive) {
        if (buzzerCard) buzzerCard.className = "flex items-center gap-3 rounded-lg p-3 ring-1 bg-red-500/20 text-red-400 ring-red-500/40 shadow-[0_0_15px_rgba(239,68,68,0.2)]";
        if (buzzerIcon) buzzerIcon.className = "fa-solid fa-volume-high text-sm buzzer-active-anim";
    } else {
        if (buzzerCard) buzzerCard.className = "flex items-center gap-3 rounded-lg p-3 ring-1 bg-black/10 text-gray-300 ring-border";
        if (buzzerIcon) buzzerIcon.className = "fa-solid fa-volume-xmark text-sm text-gray-500";
    }

    // -- LEDs -----------------------------------------------------------------
    const ledRed    = document.getElementById("led-red");
    const ledYellow = document.getElementById("led-yellow");
    const ledGreen  = document.getElementById("led-green");
    const valLed    = document.getElementById("val-actuator-led");
    const metaLed   = document.getElementById("meta-led");
    if (ledRed)    ledRed.className    = "h-3.5 w-3.5 rounded-full transition-all opacity-20";
    if (ledYellow) ledYellow.className = "h-3.5 w-3.5 rounded-full transition-all opacity-20";
    if (ledGreen)  ledGreen.className  = "h-3.5 w-3.5 rounded-full transition-all opacity-20";

    if (!relayActive || buzzerActive) {
        if (ledRed)   ledRed.className   = "h-3.5 w-3.5 rounded-full transition-all glow-red";
        if (valLed)  { valLed.innerText  = "RED";    valLed.className  = "font-mono text-sm font-semibold text-red-500"; }
        if (metaLed) { metaLed.innerText = "RED";    metaLed.className = "text-sm font-semibold text-red-500"; }
    } else if (isSpinning) {
        if (ledYellow) ledYellow.className = "h-3.5 w-3.5 rounded-full transition-all glow-yellow";
        if (valLed)  { valLed.innerText  = "YELLOW"; valLed.className  = "font-mono text-sm font-semibold text-amber-400"; }
        if (metaLed) { metaLed.innerText = "YELLOW"; metaLed.className = "text-sm font-semibold text-amber-400"; }
    } else {
        if (ledGreen)  ledGreen.className  = "h-3.5 w-3.5 rounded-full transition-all glow-green";
        if (valLed)  { valLed.innerText  = "GREEN";  valLed.className  = "font-mono text-sm font-semibold text-emerald-400"; }
        if (metaLed) { metaLed.innerText = "GREEN";  metaLed.className = "text-sm font-semibold text-emerald-400"; }
    }

    syncSelectorButtons("fan", commands.fan_mode);
    syncSelectorButtons("relay", commands.relay_mode);
}

function syncSelectorButtons(actuator, mode) {
    ["auto","on","off"].forEach(m => {
        const btn = document.getElementById(`btn-${actuator}-${m}`);
        if (btn) btn.className = "flex-1 rounded py-1 text-xs font-semibold uppercase tracking-wider transition text-gray-400 hover:text-white";
    });
    const active = document.getElementById(`btn-${actuator}-${mode}`);
    if (active) active.className = "flex-1 rounded py-1 text-xs font-semibold uppercase tracking-wider transition bg-primary text-gray-900 shadow-md";
}

async function setActuatorMode(actuator, mode) {
    try {
        await fetch("/send_command", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ device: `${actuator}_mode`, value: mode }) });
        await pollSensorData();
    } catch (e) { console.error("Mode change error:", e); }
}

async function triggerResetAlarm() {
    try {
        await fetch("/send_command", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ device: "reset_alarm", value: true }) });
        await pollSensorData();
    } catch (e) { console.error("Reset alarm error:", e); }
}

function updateSelfHealingChecklist(telemetry) {
    if (!telemetry) return;
    updateCheckState("temp-warn", telemetry.temperature < 45.0);
    updateCheckState("temp-crit", telemetry.temperature < 55.0);
    updateCheckState("volt", telemetry.voltage > 3.2 && telemetry.voltage < 4.25);
    updateCheckState("current", telemetry.current <= 5.0);
}

function updateCheckState(ruleId, isOk) {
    const dot = document.getElementById(`chk-dot-${ruleId}`);
    const lbl = document.getElementById(`chk-lbl-${ruleId}`);
    if (!dot || !lbl) return;
    if (isOk) {
        dot.className = "h-2 w-2 rounded-full bg-primary";
        lbl.innerText = "OK"; lbl.className = "ml-auto text-gray-400 font-bold";
    } else {
        dot.className = "h-2 w-2 rounded-full bg-red-500 animate-pulse";
        lbl.innerText = "TRIPPED"; lbl.className = "ml-auto text-red-500 font-bold";
    }
}

function updateLogsList(logs) {
    const c = document.getElementById("logs-list-container");
    if (!c) return;
    if (!logs || logs.length === 0) { c.innerHTML = `<li class="text-gray-500 text-center py-4">Awaiting logs...</li>`; return; }
    c.innerHTML = logs.map(log => {
        let timeStr = "--:--:--";
        if (log.timestamp) {
            const raw = log.timestamp.includes("T") ? log.timestamp : log.timestamp.replace(" ", "T");
            const d = new Date(raw);
            timeStr = isNaN(d.getTime()) ? log.timestamp : d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
        }
        let dot = "text-primary";
        if (log.event === "HEALING_ACTION") dot = "text-amber-500";
        else if (log.event === "RECOVERY") dot = "text-emerald-400";
        return `<li class="flex gap-2 border-b border-white/5 pb-1.5 leading-normal">
            <span class="text-gray-500 text-[10px] flex-shrink-0">${timeStr}</span>
            <span class="${dot}">•</span>
            <span class="flex-1 text-gray-300 text-left">${log.description}</span>
        </li>`;
    }).join("");
}

function updateTrendsCharts(chartData) {
    if (!chartData || !chartData.timestamps || chartData.timestamps.length === 0) return;
    tempChart.data.labels    = chartData.timestamps;
    tempChart.data.datasets[0].data = chartData.temperature;
    tempChart.update("none");
    voltageChart.data.labels = chartData.timestamps;
    voltageChart.data.datasets[0].data = chartData.voltage;
    voltageChart.update("none");
    currentChart.data.labels = chartData.timestamps;
    currentChart.data.datasets[0].data = chartData.current;
    currentChart.update("none");
}

function updateSliders() {
    const loadVal = parseFloat(document.getElementById("slide-load").value);
    const tempVal = parseFloat(document.getElementById("slide-temp").value);
    document.getElementById("lbl-slide-load").innerText = `${Math.round(loadVal * 100)}%`;
    document.getElementById("lbl-slide-temp").innerText = `${tempVal}C`;
    runSimulationTick();
}

function injectFault(mode) {
    activeFaultMode = mode;
    ["none","overheat","overvolt","overcurrent"].forEach(k => {
        const btn = document.getElementById(`btn-fault-${k}`);
        if (btn) btn.className = "flex-1 rounded py-1.5 text-[11px] font-semibold uppercase tracking-wider transition text-gray-400 hover:text-white";
    });
    const ab = document.getElementById(`btn-fault-${mode}`);
    if (ab) ab.className = "flex-1 rounded py-1.5 text-[11px] font-semibold uppercase tracking-wider transition bg-primary text-gray-900 shadow-md";
    if (simulationInterval) { clearInterval(simulationInterval); simulationInterval = null; }
    runSimulationTick();
    if (mode !== "none") simulationInterval = setInterval(runSimulationTick, 1000);
}

async function runSimulationTick() {
    const loadVal     = parseFloat(document.getElementById("slide-load").value);
    const ambientTemp = parseFloat(document.getElementById("slide-temp").value);

    let current     = loadVal * 4.5;
    let temperature = ambientTemp + (current * current * 1.25);
    let voltage     = 4.15 - (loadVal * 0.4);

    if (activeFaultMode === "overheat") {
        temperature = 58.5; current = 1.0; voltage = 3.85;
    } else if (activeFaultMode === "overvolt") {
        voltage = 4.35; temperature = 26.5; current = 0.5;
    } else if (activeFaultMode === "overcurrent") {
        current = 5.50; temperature = 28.0; voltage = 3.90;
    }

    const payload = {
        temperature: Math.max(15, temperature + (Math.random() - 0.5) * 0.3),
        humidity:    55.0 + (Math.random() - 0.5) * 1.0,
        voltage:     Math.max(2.5, voltage + (Math.random() - 0.5) * 0.01),
        current:     Math.max(0, current + (Math.random() - 0.5) * 0.04)
    };

    try {
        const r = await fetch("/update_sensor_data", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload)
        });
        if (r.ok) await pollSensorData();
    } catch (e) { console.error("Simulation tick error:", e); }
}
