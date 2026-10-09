// EV Battery Digital Twin Dashboard Controller
// Polls /get_sensor_data every second, drives all UI elements & Web Audio Buzzer sound.

'use strict';

let tempChart = null, voltageChart = null, currentChart = null;
let activeFaultMode = 'none', simulationInterval = null, realTimeLogInterval = null;
let evSimulationLog = [];
let evLastLoggedTemperature = null;
let lastDataTimestamp = null;
let sliderLoad = 0.40, sliderTemp = 28;

// Central EV Digital Twin simulation state.
// 1 simulated hour = 20 real seconds (change DEMO_SECONDS_PER_SIM_HOUR for a faster/slower viva demo).
const DEMO_SECONDS_PER_SIM_HOUR = 20;
const HIGH_RISK_TEMP_C = 45;
const FAN_ON_TEMP_C = 40;
const FAN_OFF_TEMP_C = 37;
const COOLING_TARGET_TEMP_C = 32;
const COOLING_RECOVERY_TEMP_C = 34;
const COOLING_MAX_DURATION_HOURS = 0.3333333333; // 20 simulated minutes
const COOLING_NOMINAL_MINUTES = 15; // nominal dashboard target; high starting temperatures may take up to 20 simulated minutes
const PACK_SERIES_CELLS = 96;
const NOMINAL_CELL_VOLTAGE = 3.7;
const MAX_CELL_VOLTAGE = 4.2;
const MIN_CELL_VOLTAGE = 3.2;
const PACK_NOMINAL_VOLTAGE = 380.0;
const PACK_MIN_NORMAL_V = 320.0;
const PACK_MAX_NORMAL_V = 420.0;
const PACK_CRITICAL_LOW_V = 280.0;
const PACK_CRITICAL_HIGH_V = 430.0;
const CURRENT_NORMAL_MAX_A = 200.0;
const CURRENT_HIGH_MAX_A = 300.0;
const PACK_CAPACITY_KWH = 60;
const PACK_INTERNAL_RESISTANCE_OHM = 0.06;

const simulation = {
    speedKmh: 0,
    selectedDurationHours: 5,
    elapsedHours: 0,
    temperature: 29,
    voltage: PACK_NOMINAL_VOLTAGE,
    cellVoltage: 3.93,
    current: 0,
    power: 0,
    soc: 75,
    energyConsumedKWh: 0,
    fanOn: false,
    running: false,
    coolingActive: false,
    coolingElapsedHours: 0,
    coolingStartTemp: 29,
    cellDeltaT: 1.8,
    lastTickMs: null
};

// Web Audio API Synthesizer Engine for Buzzer Sounds
let audioCtx = null;
let buzzerAudioInterval = null;
let isAudioMuted = false;
let currentPlayingFault = null;
let playedFaultSirens = {}; // Track completed 3s sirens so they stay off afterwards

function unlockAudioContext() {
    if (!audioCtx) {
        var AudioCtx = window.AudioContext || window.webkitAudioContext;
        if (AudioCtx) {
            audioCtx = new AudioCtx();
        }
    }
    if (audioCtx && audioCtx.state === 'suspended') {
        audioCtx.resume();
    }
}

document.addEventListener('click', unlockAudioContext);
document.addEventListener('keydown', unlockAudioContext);

function stopBuzzerSound() {
    if (buzzerAudioInterval) {
        clearInterval(buzzerAudioInterval);
        buzzerAudioInterval = null;
    }
    if (currentPlayingFault) {
        playedFaultSirens[currentPlayingFault] = true; // Mark 3s siren completed
    }
    currentPlayingFault = null;
    updateBuzzerAudioBadge(false, 'Sound Idle');
}

function playTone(freq, durationMs, type) {
    if (isAudioMuted) return;
    unlockAudioContext();
    if (!audioCtx) return;
    try {
        type = type || 'sawtooth';
        var osc = audioCtx.createOscillator();
        var gain = audioCtx.createGain();
        osc.type = type;
        osc.frequency.setValueAtTime(freq, audioCtx.currentTime);
        
        gain.gain.setValueAtTime(0.25, audioCtx.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + (durationMs / 1000));
        
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        
        osc.start();
        osc.stop(audioCtx.currentTime + (durationMs / 1000));
    } catch(err) {
        console.warn('Audio tone err:', err);
    }
}

function startBuzzerAlarm(faultType, force) {
    if (isAudioMuted) return;
    if (!force && playedFaultSirens[faultType]) return; // Siren already played for 3s, stay off
    if (!force && currentPlayingFault === faultType && buzzerAudioInterval) return;
    
    stopBuzzerSound();
    currentPlayingFault = faultType;
    playedFaultSirens[faultType] = false;
    unlockAudioContext();

    let startTime = Date.now();
    const DURATION_MS = 3000; // Exact 3 second siren for every fault injector
    let step = 0;

    if (faultType === 'overheat') {
        updateBuzzerAudioBadge(true, '🔊 OVERHEAT ALARM BUZZER (3s)');
        buzzerAudioInterval = setInterval(function() {
            if (isAudioMuted || (Date.now() - startTime >= DURATION_MS)) { stopBuzzerSound(); return; }
            var freq = (step % 2 === 0) ? 880 : 1046;
            playTone(freq, 180, 'square');
            step++;
        }, 250);
    } else if (faultType === 'overvolt') {
        updateBuzzerAudioBadge(true, '⚡ OVERVOLT ALARM BUZZER (3s)');
        buzzerAudioInterval = setInterval(function() {
            if (isAudioMuted || (Date.now() - startTime >= DURATION_MS)) { stopBuzzerSound(); return; }
            var freq = (step % 2 === 0) ? 1600 : 1200;
            playTone(freq, 140, 'sine');
            step++;
        }, 200);
    } else if (faultType === 'overcurrent') {
        updateBuzzerAudioBadge(true, '⚡ OVERCURRENT ALARM BUZZER (3s)');
        buzzerAudioInterval = setInterval(function() {
            if (isAudioMuted || (Date.now() - startTime >= DURATION_MS)) { stopBuzzerSound(); return; }
            playTone(1350, 100, 'sawtooth');
            step++;
        }, 150);
    } else {
        updateBuzzerAudioBadge(true, '🚨 BUZZER SIREN ACTIVE (3s)');
        buzzerAudioInterval = setInterval(function() {
            if (isAudioMuted || (Date.now() - startTime >= DURATION_MS)) { stopBuzzerSound(); return; }
            playTone(750, 180, 'sine');
            step++;
        }, 250);
    }
}

function updateBuzzerAudioBadge(active, statusText) {
    var el = document.getElementById('buzzer-audio-status');
    if (el) {
        if (active) {
            el.innerHTML = '<span class="px-2 py-0.5 rounded bg-red-500/20 border border-red-500/40 text-red-400 font-bold text-[11px] animate-pulse">' + statusText + '</span>';
        } else {
            el.innerHTML = '<span class="px-2 py-0.5 rounded bg-black/40 border border-border text-gray-400 text-[10px]">' + statusText + '</span>';
        }
    }
}

function toggleMuteBuzzerAudio() {
    isAudioMuted = !isAudioMuted;
    if (isAudioMuted) {
        stopBuzzerSound();
        updateBuzzerAudioBadge(false, '🔇 Sound Muted');
    }
}

document.addEventListener('DOMContentLoaded', function() {
    initCharts();
    pollSensorData();
    setInterval(pollSensorData, 1000);
    initializeEVSimulationUI();
    updateSimulation(false);
});

function initCharts() {
    var base = {
        responsive: true, maintainAspectRatio: false,
        animation: { duration: 300 },
        scales: {
            y: { grid: { color: 'rgba(255,255,255,0.04)' }, ticks: { color: '#64748b', font: { size: 9 } } },
            x: { grid: { display: false }, ticks: { color: '#64748b', font: { size: 9 }, maxTicksLimit: 8 } }
        },
        plugins: {
            legend: { display: false },
            tooltip: { backgroundColor: 'rgba(5,8,12,0.9)', titleColor: '#9ca3af', bodyColor: '#f3f4f6', borderColor: 'rgba(0,242,254,0.2)', borderWidth: 1 }
        }
    };
    function mkDS(label, clr) {
        return { label: label, data: [], borderColor: clr, backgroundColor: clr.replace('rgb(','rgba(').replace(')',',0.07)'), borderWidth: 1.8, tension: 0.3, fill: true, pointRadius: 2, pointHoverRadius: 5 };
    }
    tempChart = new Chart(document.getElementById('tempChart').getContext('2d'), {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                mkDS('Temp C', 'rgb(0,242,254)'),
                { label: 'Fan ON threshold', data: [], borderColor: 'rgb(245,158,11)', borderWidth: 1, borderDash: [5,5], pointRadius: 0, fill: false },
                { label: 'High threshold', data: [], borderColor: 'rgb(239,68,68)', borderWidth: 1, borderDash: [5,5], pointRadius: 0, fill: false }
            ]
        },
        options: JSON.parse(JSON.stringify(base))
    });
    voltageChart = new Chart(document.getElementById('voltageChart').getContext('2d'), { type: 'line', data: { labels: [], datasets: [mkDS('Voltage V', 'rgb(16,185,129)' )] }, options: JSON.parse(JSON.stringify(base)) });
    currentChart = new Chart(document.getElementById('currentChart').getContext('2d'), { type: 'line', data: { labels: [], datasets: [mkDS('Current A', 'rgb(245,158,11)' )] }, options: JSON.parse(JSON.stringify(base)) });
}

async function pollSensorData() {
    try {
        var res = await fetch('/get_sensor_data');
        if (!res.ok) return;
        var d = await res.json();
        if (d.telemetry) lastDataTimestamp = Date.now();
        updateESP32Status();
        updateSystemStatus(d);
        updateTelemetryMetrics(d.telemetry, d.health);
        updateBatteryVisual(d.telemetry, d.commands);
        updateActuators(d.commands);
        if (d.commands && simulation && d.commands.fan_mode === 'auto') {
            simulation.fanOn = !!d.commands.fan;
            renderSimulationUI();
        }
        updateSelfHealingChecklist(d.telemetry);
        updateAlertsBanner(d.alerts);
        updateLogsList(d.logs);
        updateTrendsCharts(d.chart_data);
        updateFutureScopeUI(d.future_scope);
        updateActuatorModeButtons(d.commands);
        checkAudioBuzzerState(d.telemetry, d.commands);
    } catch (err) { console.warn('[Poll]', err.message); }
}

function checkAudioBuzzerState(t, cmd) {
    if (!t && !cmd) return;
    var isOverheat = (activeFaultMode === 'overheat') || (t && t.temperature >= 45.0);
    var isOvervolt = (activeFaultMode === 'overvolt') || (t && (t.voltage > 430 || t.voltage < 280));
    var isOvercurrent = (activeFaultMode === 'overcurrent') || (t && t.current > 300);
    var isBuzzerCmd = (cmd && cmd.buzzer);

    if (isOverheat) {
        if (!playedFaultSirens['overheat']) startBuzzerAlarm('overheat');
    } else if (isOvervolt) {
        if (!playedFaultSirens['overvolt']) startBuzzerAlarm('overvolt');
    } else if (isOvercurrent) {
        if (!playedFaultSirens['overcurrent']) startBuzzerAlarm('overcurrent');
    } else if (isBuzzerCmd) {
        if (!playedFaultSirens['general']) startBuzzerAlarm('general');
    } else {
        if (activeFaultMode === 'none') {
            stopBuzzerSound();
        }
    }
}

function setText(id, val) { var el = document.getElementById(id); if (el) el.textContent = val; }

function updateESP32Status() {
    var live  = lastDataTimestamp && (Date.now() - lastDataTimestamp < 5000);
    var color = live ? '#34d399' : '#ef4444';
    var d1 = document.getElementById('esp32-status-dot');
    var d2 = document.getElementById('wifi-status-dot');
    if (d1) d1.style.backgroundColor = color;
    if (d2) d2.style.backgroundColor = color;
}

function updateSystemStatus(d) {
    var alerts = d.alerts || [], cmd = d.commands || {}, health = d.health || {}, telemetry = d.telemetry || {};
    var st = 'Nominal', sc = 'text-emerald-400', desc = 'Digital Twin reconciling sensor stream every second.', led = 'GREEN';
    
    var isTempOut = telemetry.temperature >= 45.0;
    var isVoltOut = telemetry.voltage > 430 || (telemetry.voltage > 0 && telemetry.voltage < 280);
    var isCurrentOut = telemetry.current > 300;
    var isWarning = telemetry.temperature >= 35 || telemetry.voltage < 320 || telemetry.voltage > 420 || telemetry.current > 200;
    var isOut = isTempOut || isVoltOut || isCurrentOut;

    if (cmd.relay === false) {
        st = 'ISOLATED - Critical Protection'; sc = 'text-red-400'; desc = 'Battery load isolated. Fault active - check alert panel.'; led = 'RED';
    } else if (isOut) {
        st = 'CRITICAL ALERT'; sc = 'text-red-400'; desc = 'Critical safety threshold breached. Awaiting reset.'; led = 'RED';
    } else if (cmd.fan || telemetry.temperature >= 40) {
        st = 'HIGH TEMPERATURE'; sc = 'text-amber-400'; desc = 'Battery temperature is elevated. Automatic cooling fan is active.'; led = 'YELLOW';
    } else if (isWarning) {
        st = 'WARNING / HIGH LOAD'; sc = 'text-amber-400'; desc = 'Battery operating outside the preferred simulation range.'; led = 'YELLOW';
    }
    
    var stEl = document.getElementById('status-text');
    if (stEl) { stEl.textContent = st; stEl.className = 'text-2xl font-semibold ' + sc; }
    setText('status-desc', desc);
    setText('meta-cycles', health.estimated_cycles != null ? health.estimated_cycles + ' cyc' : '--');
    setText('meta-life', health.estimated_remaining_life != null ? health.estimated_remaining_life + ' cyc' : '--');
    setText('meta-led', led);
    var p = document.getElementById('status-pulse');
    if (p) p.style.backgroundColor = (cmd.relay === false || isOut) ? '#ef4444' : (cmd.fan || telemetry.temperature >= 40) ? '#f59e0b' : '#10b981';
}

function updateTelemetryMetrics(t, h) {
    if (!t) return;
    setText('metric-temp',     t.temperature    != null ? t.temperature.toFixed(1)    : '--.-');
    setText('metric-voltage',  t.voltage        != null ? t.voltage.toFixed(3)        : '--.---');
    setText('metric-current',  t.current        != null ? t.current.toFixed(2)        : '--.--');
    setText('metric-power',    t.power          != null ? (t.power / 1000).toFixed(1) : '--.-');
    setText('metric-humidity', t.humidity       != null ? t.humidity.toFixed(1)       : '--.-');
    setText('metric-soc',      t.battery_percentage != null ? t.battery_percentage.toFixed(1) : '--.-');
    if (h) {
        setText('metric-soh',   h.health_score != null ? h.health_score.toFixed(1) : '--.-');
        setText('metric-health',h.health_score != null ? h.health_score.toFixed(0) : '--');
        setText('metric-eff',   h.efficiency   != null ? h.efficiency.toFixed(1)   : '--.-');
    }
    setText('txt-soc',     (t.battery_percentage != null ? t.battery_percentage.toFixed(1) : '--.-') + ' %');
    setText('txt-soh',     (h && h.health_score  != null ? h.health_score.toFixed(1) : '--.-') + ' %');
    setText('txt-cell-voltage', (t.cell_voltage != null ? t.cell_voltage.toFixed(2) : (t.voltage != null ? (t.voltage / 96).toFixed(2) : '--.--')) + ' V');
    setText('txt-voltage', (t.voltage            != null ? t.voltage.toFixed(3)      : '--.---') + ' V');
    setText('txt-current', (t.current            != null ? t.current.toFixed(2)      : '--.--') + ' A');
    setText('txt-temp',    (t.temperature        != null ? t.temperature.toFixed(1)  : '--.-') + ' C');
    setText('txt-power',   (t.power              != null ? t.power.toFixed(2)        : '--.--') + ' W');
    setText('txt-eff',     (h && h.efficiency    != null ? h.efficiency.toFixed(1)   : '--.-') + ' %');
}

function updateBatteryVisual(t, cmd) {
    if (!t) return;
    var soc = t.battery_percentage || 0;
    var bar = document.getElementById('battery-fill-bar');
    var pct = document.getElementById('battery-fill-percent');
    var box = document.getElementById('twin-battery-box');
    var loadEl = document.getElementById('txt-load');
    if (bar) {
        var color = 'linear-gradient(180deg,#00f2fe,#0ea5e9)';
        if (soc < 20)      color = 'linear-gradient(180deg,#ef4444,#b91c1c)';
        else if (soc < 40) color = 'linear-gradient(180deg,#f59e0b,#d97706)';
        bar.style.height = soc + '%';
        bar.style.background = color;
    }
    if (pct) pct.textContent = Math.round(soc) + '%';
    if (box) { box.classList.remove('critical-alert'); if (cmd && !cmd.relay) box.classList.add('critical-alert'); }
    if (loadEl) {
        loadEl.textContent = (cmd && cmd.relay) ? 'Connected' : 'ISOLATED';
        loadEl.style.color = (cmd && cmd.relay) ? '#34d399'    : '#ef4444';
    }
}

function updateActuators(cmd) {
    if (!cmd) return;
    var fanCard  = document.getElementById('card-actuator-fan');
    var fanIcon  = document.getElementById('icon-actuator-fan');
    var fanVal   = document.getElementById('val-actuator-fan');
    if (fanVal)  fanVal.textContent = cmd.fan ? 'ON' : 'OFF';
    if (fanCard) fanCard.className  = cmd.fan ? 'flex items-center gap-3 rounded-lg p-3 ring-1 bg-amber-500/15 text-amber-400 ring-amber-400/40' : 'flex items-center gap-3 rounded-lg p-3 ring-1 bg-black/10 text-gray-300 ring-border';
    if (fanIcon) fanIcon.className  = cmd.fan ? 'fa-solid fa-fan text-sm text-amber-400 spin-fan-active' : 'fa-solid fa-fan text-sm text-gray-400';

    var relayCard = document.getElementById('card-actuator-relay');
    var relayVal  = document.getElementById('val-actuator-relay');
    if (relayVal)  relayVal.textContent = cmd.relay ? 'CONNECTED' : 'ISOLATED';
    if (relayCard) relayCard.className  = cmd.relay ? 'flex items-center gap-3 rounded-lg p-3 ring-1 bg-emerald-500/15 text-emerald-400 ring-emerald-400/40' : 'flex items-center gap-3 rounded-lg p-3 ring-1 relay-tripped-card text-red-400';

    var buzzerCard = document.getElementById('card-actuator-buzzer');
    var buzzerIcon = document.getElementById('icon-actuator-buzzer');
    var buzzerVal  = document.getElementById('val-actuator-buzzer');
    if (buzzerVal)  buzzerVal.textContent = cmd.buzzer ? 'ALARM ON' : 'OFF';
    if (buzzerCard) buzzerCard.className  = cmd.buzzer ? 'flex items-center gap-3 rounded-lg p-3 ring-1 bg-red-500/15 text-red-400 ring-red-400/40' : 'flex items-center gap-3 rounded-lg p-3 ring-1 bg-black/10 text-gray-300 ring-border';
    if (buzzerIcon) buzzerIcon.className  = cmd.buzzer ? 'fa-solid fa-volume-high text-sm buzzer-active-anim' : 'fa-solid fa-volume-high text-sm';

    var ledRed    = document.getElementById('led-red');
    var ledYellow = document.getElementById('led-yellow');
    var ledGreen  = document.getElementById('led-green');
    var ledVal    = document.getElementById('val-actuator-led');
    [ledRed, ledYellow, ledGreen].forEach(function(el) { if (el) { el.style.opacity = '0.2'; el.style.boxShadow = 'none'; } });
    if (!cmd.relay) {
        if (ledRed)    { ledRed.style.opacity = '1';    ledRed.style.boxShadow    = '0 0 14px #ef4444, 0 0 24px #ef4444'; }
        if (ledVal)    ledVal.textContent = 'RED';
    } else if (cmd.fan || cmd.buzzer) {
        if (ledYellow) { ledYellow.style.opacity = '1'; ledYellow.style.boxShadow = '0 0 14px #f59e0b, 0 0 24px #f59e0b'; }
        if (ledVal)    ledVal.textContent = 'YELLOW';
    } else {
        if (ledGreen)  { ledGreen.style.opacity  = '1'; ledGreen.style.boxShadow  = '0 0 14px #10b981, 0 0 24px #10b981'; }
        if (ledVal)    ledVal.textContent = 'GREEN';
    }
}

function updateSelfHealingChecklist(t) {
    if (!t) return;
    _setCheck('temp-warn', t.temperature < 40,  t.temperature < 40 ? 'OK' : t.temperature.toFixed(1) + 'C');
    _setCheck('temp-crit', t.temperature < 45,  t.temperature < 45 ? 'OK' : t.temperature.toFixed(1) + 'C');
    _setCheck('volt',      t.voltage >= 320 && t.voltage <= 420, (t.voltage >= 320 && t.voltage <= 420) ? 'OK' : t.voltage.toFixed(1) + 'V');
    _setCheck('current',   t.current <= 200,    t.current <= 200 ? 'OK' : t.current.toFixed(2) + 'A');
}

function _setCheck(id, ok, lbl) {
    var dot = document.getElementById('chk-dot-' + id);
    var el  = document.getElementById('chk-lbl-' + id);
    if (dot) { dot.style.backgroundColor = ok ? '#10b981' : '#ef4444'; dot.style.boxShadow = ok ? '0 0 6px #10b981' : '0 0 6px #ef4444'; }
    if (el)  { el.textContent = lbl; el.style.color = ok ? '#10b981' : '#ef4444'; }
}

function updateAlertsBanner(alerts) {
    var c = document.getElementById('alerts-container');
    if (!c) return;
    c.innerHTML = '';
    (alerts || []).forEach(function(a) {
        var crit = a.severity === 'CRITICAL';
        var ts   = a.timestamp ? a.timestamp.replace('T',' ').substring(0,19) : '';
        var div  = document.createElement('div');
        div.className = 'flex items-start gap-3 p-4 rounded-lg border text-sm ' + (crit ? 'bg-red-500/10 border-red-500/40 text-red-300' : 'bg-amber-500/10 border-amber-500/40 text-amber-300');
        div.innerHTML = '<i class="fa-solid ' + (crit ? 'fa-circle-xmark text-red-400' : 'fa-triangle-exclamation text-amber-400') + ' mt-0.5 shrink-0"></i>' +
            '<div class="flex-1"><span class="font-bold uppercase tracking-wide text-xs">[' + a.severity + '] ' + a.alert_type + '</span><p class="text-xs mt-0.5 opacity-80">' + a.message + '</p></div>' +
            '<span class="font-mono text-[10px] opacity-50 shrink-0 mt-0.5">' + ts + '</span>';
        c.appendChild(div);
    });
}

function updateLogsList(logs) {
    var ul = document.getElementById('logs-list-container');
    if (!ul || !logs || !logs.length) return;
    ul.innerHTML = logs.map(function(l) {
        var ts  = l.timestamp ? l.timestamp.replace('T',' ').substring(11,19) : '--:--:--';
        var dot = (l.event && l.event.indexOf('RECOVERY') === 0) ? 'text-emerald-400'
                : (l.event && l.event.indexOf('HEALING')  === 0) ? 'text-red-400'
                : (l.event && l.event.indexOf('AUTH')     === 0) ? 'text-blue-400' : 'text-primary';
        return '<li class="flex gap-2 border-b border-border/10 pb-1.5"><span class="text-gray-500 shrink-0">' + ts + '</span><span class="' + dot + ' shrink-0">&#8226;</span><span class="flex-1 text-gray-300 break-words">' + (l.description || l.event) + '</span></li>';
    }).join('');
}

function updateTrendsCharts(cd) {
    if (!cd) return;
    _pushChart(tempChart,    cd.timestamps, cd.temperature);
    _pushChart(voltageChart, cd.timestamps, cd.voltage);
    _pushChart(currentChart, cd.timestamps, cd.current);
}

function _pushChart(chart, labels, data) {
    if (!chart || !labels || !data) return;
    chart.data.labels = labels;
    chart.data.datasets[0].data = data;
    if (chart === tempChart) {
        chart.data.datasets[1].data = labels.map(function() { return 40; });
        chart.data.datasets[2].data = labels.map(function() { return 45; });
    }
    chart.update('none');
}

function updateActuatorModeButtons(cmd) {
    if (!cmd) return;
    _syncSel('fan',   cmd.fan_mode   || 'auto');
    _syncSel('relay', cmd.relay_mode || 'auto');
}

function _syncSel(act, mode) {
    ['auto','on','off'].forEach(function(m) {
        var b = document.getElementById('btn-' + act + '-' + m);
        if (!b) return;
        b.className = 'flex-1 rounded py-1 text-xs font-semibold uppercase tracking-wider transition ' + (m === mode ? 'bg-primary text-gray-900' : 'text-gray-400 hover:text-white');
    });
}

async function setActuatorMode(actuator, mode) {
    _syncSel(actuator, mode);
    try {
        await fetch('/send_command', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ device: actuator + '_mode', value: mode }) });
        pollSensorData();
    } catch(e) { console.error(e); }
}

async function triggerResetAlarm() {
    stopBuzzerSound();
    activeFaultMode = 'none';
    playedFaultSirens = {};
    if (simulationInterval) { clearInterval(simulationInterval); simulationInterval = null; }
    ['none','overheat','overvolt','overcurrent'].forEach(function(m) {
        var b = document.getElementById('btn-fault-' + m);
        if (b) b.className = 'flex-1 rounded py-1 text-[11px] font-semibold uppercase tracking-wider transition ' + (m === 'none' ? 'bg-primary text-gray-900' : 'text-gray-400 hover:text-white');
    });

    try {
        await fetch('/send_command', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ device: 'reset_alarm', value: true }) });
        await fetch('/update_sensor_data', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ source: 'esp32', temperature: 28.0, humidity: 45.0, voltage: 3.85, current: 1.20 })
        });
        pollSensorData();
    } catch(e) { console.error(e); }
}


function updateSliders() {
    sliderLoad = parseFloat(document.getElementById('slide-load') ? document.getElementById('slide-load').value : 0.4);
    sliderTemp = parseFloat(document.getElementById('slide-temp') ? document.getElementById('slide-temp').value : 28);
    var ll = document.getElementById('lbl-slide-load'); if (ll) ll.textContent = Math.round(sliderLoad * 100) + '%';
    var lt = document.getElementById('lbl-slide-temp'); if (lt) lt.textContent = sliderTemp + 'C';
    // Fault Injector remains separate from the central EV driving simulation.
    if (activeFaultMode !== 'none') runFaultSimulationTick();
}

function initializeEVSimulationUI() {
    var speed = document.getElementById('ev-speed-slider');
    var duration = document.getElementById('ev-duration-select');
    if (speed) speed.value = simulation.speedKmh;
    if (duration) duration.value = String(simulation.selectedDurationHours);
    renderSimulationUI();
}

function clamp(v, min, max) {
    return Math.min(max, Math.max(min, v));
}

function setEVSpeed(value) {
    simulation.speedKmh = clamp(Number(value) || 0, 0, 120);
    renderSimulationUI();
    updateSimulation(false);
}

function setEVDuration(value) {
    simulation.selectedDurationHours = clamp(Number(value) || 0, 0, 5);
    simulation.elapsedHours = Math.min(simulation.elapsedHours, simulation.selectedDurationHours);
    renderSimulationUI();
    updateSimulation(false);
}

function startEVSimulation() {
    if (simulation.coolingActive) return;
    if (simulation.selectedDurationHours <= 0) {
        simulation.selectedDurationHours = 5;
        var sel = document.getElementById('ev-duration-select');
        if (sel) sel.value = '5';
    }
    // Never choose a speed automatically. The driver controls the slider.
    // At 0 km/h the timer can run, but no driving load is generated.
    simulation.running = true;
    simulation.lastTickMs = performance.now();
    if (simulationInterval) clearInterval(simulationInterval);
    simulationInterval = setInterval(function() { updateSimulation(true); }, 1000);
    startRealTimeDataLog();
    renderSimulationUI();
    updateSimulation(false);
}

function pauseEVSimulation() {
    simulation.running = false;
    simulation.lastTickMs = null;
    if (simulationInterval) {
        clearInterval(simulationInterval);
        simulationInterval = null;
    }
    stopRealTimeDataLog();
    renderSimulationUI();
}

function resetEVSimulation() {
    pauseEVSimulation();
    stopRealTimeDataLog();
    activeFaultMode = 'none';
    playedFaultSirens = {};
    stopBuzzerSound();

    simulation.speedKmh = 0;
    simulation.selectedDurationHours = 5;
    simulation.elapsedHours = 0;
    simulation.temperature = 29;
    simulation.voltage = PACK_NOMINAL_VOLTAGE;
    simulation.cellVoltage = cellVoltageFromSOC(simulation.soc);
    simulation.current = 0;
    simulation.power = 0;
    simulation.soc = 75;
    simulation.energyConsumedKWh = 0;
    simulation.fanOn = false;
    simulation.coolingActive = false;
    simulation.coolingElapsedHours = 0;
    simulation.coolingStartTemp = 29;
    simulation.cellDeltaT = 1.8;
    clearEVSimulationLog();

    var speed = document.getElementById('ev-speed-slider');
    var duration = document.getElementById('ev-duration-select');
    if (speed) speed.value = '0';
    if (duration) duration.value = '5';

    renderSimulationUI();
    updateSimulation(false);

    // Keep the existing backend command path; AUTO remains the default.
    setActuatorMode('fan', 'auto');
}


function formatSimClock(hours) {
    var totalSeconds = Math.max(0, Math.round(hours * 3600));
    var h = Math.floor(totalSeconds / 3600);
    var m = Math.floor((totalSeconds % 3600) / 60);
    var sec = totalSeconds % 60;
    return String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0') + ':' + String(sec).padStart(2, '0');
}

function formatRealClock(date) {
    return date.toLocaleTimeString([], { hour12: false });
}

function startRealTimeDataLog() {
    if (realTimeLogInterval) clearInterval(realTimeLogInterval);
    // Exactly one log row per real-world second. The accelerated simulation clock
    // is intentionally separate from this wall-clock sampling interval.
    realTimeLogInterval = setInterval(function() {
        if (simulation.running || simulation.coolingActive) appendEVSimulationLog();
    }, 1000);
}

function stopRealTimeDataLog() {
    if (realTimeLogInterval) {
        clearInterval(realTimeLogInterval);
        realTimeLogInterval = null;
    }
}

function clearEVSimulationLog() {
    evSimulationLog = [];
    evLastLoggedTemperature = null;
    var body = document.getElementById('ev-live-log-body');
    if (body) {
        body.innerHTML = '<tr><td colspan="13" class="py-6 text-center text-gray-600">Start the simulation to record one row every real-world second.</td></tr>';
    }
    setText('ev-log-count', '0 records');
}

function appendEVSimulationLog() {
    var deltaT = evLastLoggedTemperature == null ? 0 : simulation.temperature - evLastLoggedTemperature;
    evLastLoggedTemperature = simulation.temperature;

    evSimulationLog.push({
        realTime: formatRealClock(new Date()),
        simTime: simulation.coolingActive ? formatSimClock(simulation.elapsedHours) + ' COOL' : formatSimClock(simulation.elapsedHours),
        speed: simulation.coolingActive ? 0 : Math.round(simulation.speedKmh),
        temp: simulation.temperature,
        deltaT: deltaT,
        fan: simulation.fanOn,
        cellVoltage: simulation.cellVoltage,
        packVoltage: simulation.voltage,
        current: simulation.current,
        power: simulation.power,
        soc: simulation.soc,
        status: temperatureStatus(simulation.temperature),
        cellDeltaT: simulation.cellDeltaT
    });

    // Keep the browser light even during a long demo. The latest 600 seconds remain visible.
    if (evSimulationLog.length > 600) evSimulationLog.shift();
    renderEVSimulationLog();
}

function renderEVSimulationLog() {
    var body = document.getElementById('ev-live-log-body');
    if (!body) return;
    if (!evSimulationLog.length) {
        body.innerHTML = '<tr><td colspan="13" class="py-6 text-center text-gray-600">Start the simulation to record one row every real-world second.</td></tr>';
        setText('ev-log-count', '0 records');
        return;
    }
    var rows = evSimulationLog.slice(-600).map(function(r) {
        var statusClass = r.status === 'CRITICAL' ? 'text-red-400' : (r.status === 'HIGH TEMPERATURE' || r.status === 'WARNING') ? 'text-amber-400' : 'text-emerald-400';
        var fanClass = r.fan ? 'text-amber-300' : 'text-gray-500';
        var dtClass = r.deltaT > 0.005 ? 'text-orange-300' : r.deltaT < -0.005 ? 'text-cyan-300' : 'text-gray-500';
        return '<tr>' +
            '<td>' + r.realTime + '</td>' +
            '<td>' + r.simTime + '</td>' +
            '<td>' + r.speed + ' km/h</td>' +
            '<td>' + r.temp.toFixed(1) + '°C</td>' +
            '<td class="' + dtClass + '">' + (r.deltaT >= 0 ? '+' : '') + r.deltaT.toFixed(2) + '°C</td>' +
            '<td class="' + fanClass + '">' + (r.fan ? 'ON' : 'OFF') + '</td>' +
            '<td>' + r.cellVoltage.toFixed(2) + ' V</td>' +
            '<td>' + r.packVoltage.toFixed(1) + ' V</td>' +
            '<td>' + r.current.toFixed(2) + ' A</td>' +
            '<td>' + (r.power / 1000).toFixed(1) + ' kW</td>' +
            '<td>' + r.soc.toFixed(2) + '%</td>' +
            '<td>' + r.cellDeltaT.toFixed(2) + '°C</td>' +
            '<td class="' + statusClass + '">' + r.status + '</td>' +
            '</tr>';
    }).join('');
    body.innerHTML = rows;
    setText('ev-log-count', evSimulationLog.length + ' records');
    var wrap = body.closest('.ev-live-log-wrap');
    if (wrap) wrap.scrollTop = wrap.scrollHeight;
}

function simulationTemperatureTarget(hours, speed) {
    // Deterministic reference curve at 60 km/h. This is the heat-load target
    // before active cooling is applied. Higher speed increases heat generation.
    const points = [
        [0.0, 29.0], [0.5, 31.0], [1.0, 33.5], [1.5, 36.0],
        [2.0, 39.0], [2.5, 42.0], [3.0, 46.0], [3.5, 48.0],
        [4.0, 50.0], [5.0, 53.0]
    ];
    var h = clamp(hours, 0, 5);
    var base = points[points.length - 1][1];
    for (var i = 1; i < points.length; i++) {
        if (h <= points[i][0]) {
            var h1 = points[i - 1][0], t1 = points[i - 1][1];
            var h2 = points[i][0], t2 = points[i][1];
            base = t1 + (t2 - t1) * ((h - h1) / (h2 - h1));
            break;
        }
    }
    var loadFactor = speed <= 0 ? 0 : Math.pow(clamp(speed / 60, 0, 2), 0.9);
    return 29 + (base - 29) * loadFactor;
}

function currentFromSpeed(speed) {
    // 400-V-class EV traction-load model. Smooth power-law mapping gives ~85 A at
    // 60 km/h and ~300 A at 120 km/h, while keeping low speeds genuinely low-load.
    var v = clamp(speed, 0, 120);
    return v <= 0 ? 0 : 85.0 * Math.pow(v / 60.0, 1.82);
}

function cellVoltageFromSOC(soc) {
    var s = clamp(soc, 0, 100);
    // Piecewise linear OCV map matching the requested demonstration points.
    const map = [[0,3.20],[10,3.40],[20,3.55],[30,3.65],[40,3.75],[50,3.80],
                 [60,3.85],[70,3.90],[80,3.95],[90,4.05],[100,4.18]];
    for (var i = 1; i < map.length; i++) {
        if (s <= map[i][0]) {
            var s1=map[i-1][0], v1=map[i-1][1], s2=map[i][0], v2=map[i][1];
            return v1 + (v2-v1) * ((s-s1)/(s2-s1));
        }
    }
    return 4.18;
}

function temperatureStatus(temp) {
    if (temp < 35) return 'NORMAL';
    if (temp < 40) return 'WARNING';
    if (temp < 45) return 'HIGH TEMPERATURE';
    return 'CRITICAL';
}

function voltageStatus(voltage) {
    if (voltage < PACK_CRITICAL_LOW_V) return 'CRITICAL LOW';
    if (voltage < PACK_MIN_NORMAL_V) return 'LOW WARNING';
    if (voltage <= PACK_MAX_NORMAL_V) return 'NORMAL';
    if (voltage <= PACK_CRITICAL_HIGH_V) return 'HIGH WARNING';
    return 'OVERVOLTAGE';
}

function currentStatus(current) {
    if (current <= CURRENT_NORMAL_MAX_A) return 'NORMAL';
    if (current <= CURRENT_HIGH_MAX_A) return 'HIGH LOAD';
    return 'CRITICAL';
}

function socStatus(soc) {
    if (soc < 10) return 'CRITICAL LOW';
    if (soc < 20) return 'LOW';
    if (soc <= 90) return 'NORMAL';
    if (soc <= 95) return 'HIGH';
    return 'CHARGE LIMIT WARNING';
}

function updateSimulation(advanceClock) {
    var simDtHours = 0;
    if (advanceClock && (simulation.running || simulation.coolingActive)) {
        var now = performance.now();
        var dtReal = simulation.lastTickMs == null ? 0 : Math.max(0, (now - simulation.lastTickMs) / 1000);
        simulation.lastTickMs = now;
        simDtHours = dtReal / DEMO_SECONDS_PER_SIM_HOUR;

        if (simulation.coolingActive) {
            simulation.coolingElapsedHours = clamp(
                simulation.coolingElapsedHours + simDtHours, 0, COOLING_MAX_DURATION_HOURS
            );
        } else if (simulation.running) {
            var remaining = Math.max(0, simulation.selectedDurationHours - simulation.elapsedHours);
            simDtHours = Math.min(simDtHours, remaining);
            simulation.elapsedHours = clamp(simulation.elapsedHours + simDtHours, 0, simulation.selectedDurationHours);
            if (simulation.elapsedHours >= simulation.selectedDurationHours) {
                simulation.elapsedHours = simulation.selectedDurationHours;
                simulation.running = false;
            }
        }
    }

    var speed = simulation.coolingActive ? 0 : clamp(simulation.speedKmh, 0, 120);
    var current = currentFromSpeed(speed);
    var power = PACK_NOMINAL_VOLTAGE * current;

    if (simDtHours > 0 && simulation.running && speed > 0) {
        simulation.energyConsumedKWh += (power * simDtHours) / 1000;
        simulation.energyConsumedKWh = clamp(simulation.energyConsumedKWh, 0, PACK_CAPACITY_KWH);
    }
    simulation.soc = clamp(100 - (simulation.energyConsumedKWh / PACK_CAPACITY_KWH) * 100, 0, 100);

    var ocvCell = cellVoltageFromSOC(simulation.soc);
    var loadedPackVoltage = PACK_SERIES_CELLS * ocvCell - PACK_INTERNAL_RESISTANCE_OHM * current;
    loadedPackVoltage = clamp(loadedPackVoltage, PACK_SERIES_CELLS * MIN_CELL_VOLTAGE, PACK_SERIES_CELLS * MAX_CELL_VOLTAGE);

    // Thermal model: one deterministic model for driving, warning, fan cooling and danger.
    // The fan reduces heat accumulation strongly while driving, so temperature tends
    // toward a stable equilibrium instead of increasing without limit.
    if (simulation.coolingActive) {
        // High-risk protection: driving is locked and the fan performs a gradual
        // 15–20 simulated-minute cooldown. This is a prototype thermal model, not
        // a claim about a specific production EV battery pack.
        simulation.fanOn = true;
        var coolingStart = Math.max(29, simulation.coolingStartTemp || simulation.temperature);
        var elapsedCooling = clamp(simulation.coolingElapsedHours, 0, COOLING_MAX_DURATION_HOURS);
        // Exponential cooling gives a smooth curve and avoids an unrealistic instant drop.
        var coolingK = 6.0;
        var cooledTemp = COOLING_TARGET_TEMP_C + (coolingStart - COOLING_TARGET_TEMP_C) * Math.exp(-coolingK * elapsedCooling);
        simulation.temperature = Math.max(COOLING_TARGET_TEMP_C, cooledTemp);
        simulation.cellDeltaT = Math.max(1.0, simulation.cellDeltaT - 0.25 * simDtHours * 60);

        // Do not release the vehicle until it is genuinely back in the safe range.
        if (simulation.temperature <= COOLING_RECOVERY_TEMP_C ||
            (simulation.coolingElapsedHours >= COOLING_MAX_DURATION_HOURS && simulation.temperature <= 46)) {
            simulation.coolingActive = false;
            simulation.coolingElapsedHours = 0;
            simulation.temperature = Math.min(simulation.temperature, COOLING_RECOVERY_TEMP_C);
            simulation.fanOn = false;
            simulation.speedKmh = 0;
            simulation.running = false;
        }
    } else {
        var rawTargetTemp = simulationTemperatureTarget(simulation.elapsedHours, speed);

        // First-order thermal response. During driving the fan lowers the effective
        // equilibrium temperature; low/medium load therefore settles instead of
        // continuously climbing. Very high load can still overwhelm cooling and
        // deliberately enter the critical protection state.
        if (simulation.fanOn) {
            var fanEffect = speed >= 100 ? 9.0 : speed >= 80 ? 12.0 : 15.0;
            var effectiveTarget = Math.max(29, rawTargetTemp - fanEffect);
            simulation.temperature += (effectiveTarget - simulation.temperature) * 0.12;
        } else {
            simulation.temperature += (rawTargetTemp - simulation.temperature) * 0.10;
        }

        // Automatic fan: ON above 40°C; OFF only after the temperature reaches 37°C.
        if (simulation.fanOn) {
            if (simulation.temperature <= FAN_OFF_TEMP_C) simulation.fanOn = false;
        } else if (simulation.temperature > FAN_ON_TEMP_C) {
            simulation.fanOn = true;
        }

        // Once the fan is on, apply an additional smooth cooling term. It is deliberately
        // small so the temperature falls/stabilizes gradually rather than jumping.
        if (simulation.fanOn) {
            var activeCoolingPerHour = speed >= 100 ? 3.0 : speed >= 80 ? 5.0 : 7.0;
            if (simDtHours > 0) {
                simulation.temperature -= activeCoolingPerHour * simDtHours;
            }
            // Prevent ordinary driving from endlessly rising while cooling is active.
            var stableCeiling = speed >= 100 ? 62.0 : speed >= 80 ? 56.0 : 52.0;
            simulation.temperature = Math.min(simulation.temperature, stableCeiling);
        }

        simulation.temperature = clamp(simulation.temperature, 20, 90);
        // Small deterministic cell-to-cell temperature spread for the pack model.
        // It rises with load and abnormal temperature but remains bounded.
        simulation.cellDeltaT = clamp(1.2 + (simulation.current / 300) * 2.4 + Math.max(0, simulation.temperature - 35) * 0.08, 0.8, 6.5);

        // ≥45°C is a safety lock. The driver cannot continue until the cooling cycle
        // returns the pack to a safe temperature.
        if (simulation.temperature >= HIGH_RISK_TEMP_C && simulation.running) {
            simulation.coolingActive = true;
            simulation.coolingElapsedHours = 0;
            simulation.coolingStartTemp = simulation.temperature;
            simulation.running = false;
            simulation.speedKmh = 0;
            simulation.fanOn = true;
        }
    }

    simulation.current = clamp(current, 0, 320);
    simulation.power = clamp(loadedPackVoltage * simulation.current, 0, 140000);
    simulation.cellVoltage = clamp(simulation.power > 0 ? loadedPackVoltage / PACK_SERIES_CELLS : ocvCell, MIN_CELL_VOLTAGE, MAX_CELL_VOLTAGE);
    simulation.voltage = clamp(loadedPackVoltage, 280, 430);

    if (!simulation.running && !simulation.coolingActive) {
        if (simulation.temperature > 40 && simulation.fanOn) {
            // Fan can continue cooling after Pause until it reaches the OFF threshold.
            simulation.temperature = clamp(simulation.temperature - 0.03, 20, 85);
            if (simulation.temperature <= FAN_OFF_TEMP_C) simulation.fanOn = false;
        }
    }

    renderSimulationUI();
    sendSimulationTelemetry();

    // Keep the cooling timer alive after a high-risk lock.
    if (simulation.coolingActive && !simulationInterval) {
        simulation.lastTickMs = performance.now();
        simulationInterval = setInterval(function() { updateSimulation(true); }, 1000);
        startRealTimeDataLog();
    }
    if (!simulation.running && !simulation.coolingActive && simulationInterval) {
        clearInterval(simulationInterval);
        simulationInterval = null;
        simulation.lastTickMs = null;
        stopRealTimeDataLog();
    }
}

function buildEVSpeedometer() {
    var ticks = document.getElementById('ev-speed-ticks');
    var labels = document.getElementById('ev-speed-labels');
    if (!ticks || !labels || ticks.dataset.ready === '1') return;

    var NS = 'http://www.w3.org/2000/svg';
    var cx = 160, cy = 160;
    var startDeg = -135, sweepDeg = 270;

    function point(radius, degrees) {
        var r = degrees * Math.PI / 180;
        return { x: cx + radius * Math.cos(r), y: cy + radius * Math.sin(r) };
    }

    for (var i = 0; i <= 60; i++) {
        var deg = startDeg + (sweepDeg * i / 60);
        var major = i % 5 === 0;
        var outer = point(116, deg);
        var inner = point(major ? 103 : 108, deg);
        var line = document.createElementNS(NS, 'line');
        line.setAttribute('x1', outer.x.toFixed(2));
        line.setAttribute('y1', outer.y.toFixed(2));
        line.setAttribute('x2', inner.x.toFixed(2));
        line.setAttribute('y2', inner.y.toFixed(2));
        line.setAttribute('class', major ? 'gauge-tick-major' : 'gauge-tick-minor');
        ticks.appendChild(line);
    }

    for (var n = 0; n <= 120; n += 20) {
        var deg = startDeg + (sweepDeg * n / 120);
        var pos = point(91, deg);
        var text = document.createElementNS(NS, 'text');
        text.setAttribute('x', pos.x.toFixed(2));
        text.setAttribute('y', pos.y.toFixed(2));
        text.setAttribute('class', n >= 100 ? 'gauge-label gauge-label-hot' : 'gauge-label');
        text.textContent = n;
        labels.appendChild(text);
    }
    ticks.dataset.ready = '1';
}


function buildEVVoltageMeter() {
    var ticks = document.getElementById('ev-voltage-ticks');
    var labels = document.getElementById('ev-voltage-labels');
    if (!ticks || !labels || ticks.dataset.ready === '1') return;

    var NS = 'http://www.w3.org/2000/svg';
    var cx = 160, cy = 160;
    var startDeg = -135, sweepDeg = 270;
    function point(radius, degrees) {
        var r = degrees * Math.PI / 180;
        return { x: cx + radius * Math.cos(r), y: cy + radius * Math.sin(r) };
    }
    for (var i = 0; i <= 54; i++) {
        var deg = startDeg + (sweepDeg * i / 54);
        var major = i % 6 === 0;
        var outer = point(116, deg);
        var inner = point(major ? 103 : 108, deg);
        var line = document.createElementNS(NS, 'line');
        line.setAttribute('x1', outer.x.toFixed(2)); line.setAttribute('y1', outer.y.toFixed(2));
        line.setAttribute('x2', inner.x.toFixed(2)); line.setAttribute('y2', inner.y.toFixed(2));
        line.setAttribute('class', major ? 'voltage-tick-major' : 'voltage-tick-minor');
        ticks.appendChild(line);
    }
    var minV = 280, maxV = 430;
    for (var v = 280; v <= 420; v += 20) {
        var deg2 = startDeg + (sweepDeg * (v - minV) / (maxV - minV));
        var pos = point(91, deg2);
        var text = document.createElementNS(NS, 'text');
        text.setAttribute('x', pos.x.toFixed(2)); text.setAttribute('y', pos.y.toFixed(2));
        text.setAttribute('class', v >= 420 || v <= 280 ? 'voltage-label voltage-label-hot' : 'voltage-label');
        text.textContent = v;
        labels.appendChild(text);
    }
    ticks.dataset.ready = '1';
}

function updateEVVoltageMeter() {
    buildEVVoltageMeter();
    var v = clamp(Number(simulation.voltage) || PACK_NOMINAL_VOLTAGE, 280, 430);
    var ratio = (v - 280) / (430 - 280);
    var angle = -135 + 270 * ratio;
    var needle = document.getElementById('ev-voltage-needle');
    if (needle) needle.style.transform = 'rotate(' + angle + 'deg)';
    var arc = document.getElementById('ev-voltage-arc');
    if (arc) arc.style.strokeDashoffset = String(792 - 792 * ratio);
    setText('ev-voltage-svg-value', v.toFixed(1));
    setText('ev-voltage-cell', (simulation.cellVoltage || NOMINAL_CELL_VOLTAGE).toFixed(2) + ' V');
    setText('ev-voltage-pack', v.toFixed(1) + ' V');
}

function renderSimulationUI() {
    buildEVSpeedometer();
    updateEVVoltageMeter();
    var speed = Math.round(clamp(simulation.speedKmh, 0, 120));
    var duration = clamp(simulation.selectedDurationHours, 0, 5);
    var elapsed = clamp(simulation.elapsedHours, 0, duration || 0);
    var pct = duration > 0 ? (elapsed / duration) * 100 : 0;
    var status = temperatureStatus(simulation.temperature);
    var statusEl = document.getElementById('ev-temp-status');

    setText('ev-speed-value', speed);
    setText('ev-speed-label', speed);
    setText('ev-duration-label', duration.toFixed(1) + ' hours');
    setText('ev-elapsed-label', elapsed.toFixed(2) + ' / ' + duration.toFixed(2) + ' hours');
    setText('ev-elapsed-value', elapsed.toFixed(2) + ' h');
    setText('ev-sim-temp', simulation.temperature.toFixed(1));
    setText('metric-cell-delta', (simulation.cellDeltaT || 0).toFixed(2));
    setText('ev-fan-status', simulation.fanOn ? 'ON' : 'OFF');
    setText('ev-timer-state', simulation.coolingActive ? 'DANGER / COOLING' : (simulation.running ? 'RUNNING' : (elapsed >= duration && duration > 0 ? 'COMPLETE' : 'STOPPED')));

    var speedSlider = document.getElementById('ev-speed-slider');
    if (speedSlider) {
        speedSlider.disabled = simulation.coolingActive;
        speedSlider.style.opacity = simulation.coolingActive ? '0.45' : '1';
    }
    var danger = document.getElementById('ev-danger-banner');
    if (danger) danger.classList.toggle('hidden', !simulation.coolingActive);
    var coolingTime = document.getElementById('ev-cooling-time');
    if (coolingTime) {
        var remainingCooling = Math.max(0, COOLING_MAX_DURATION_HOURS - simulation.coolingElapsedHours) * 60;
        var mins = Math.floor(remainingCooling / 60);
        var secs = Math.floor(remainingCooling % 60);
        coolingTime.textContent = String(mins).padStart(2, '0') + ':' + String(secs).padStart(2, '0');
    }

    var progress = document.getElementById('ev-duration-progress');
    if (progress) progress.style.width = clamp(pct, 0, 100) + '%';

    if (statusEl) {
        statusEl.textContent = status;
        statusEl.className = 'mt-1 font-mono text-xl ' +
            (status === 'CRITICAL' ? 'text-red-400' : (status === 'HIGH TEMPERATURE' || status === 'WARNING') ? 'text-amber-400' : 'text-emerald-400');
    }

    var needle = document.getElementById('ev-speed-needle');
    if (needle) needle.style.transform = 'rotate(' + (-135 + (270 * speed / 120)) + 'deg)';

    var arc = document.getElementById('ev-speed-arc');
    if (arc) {
        var arcLength = 792;
        arc.style.strokeDashoffset = String(arcLength - (arcLength * speed / 120));
    }

    setText('ev-speed-svg-value', speed);
    setText('ev-gauge-load', Math.round(speed / 120 * 100) + '%');
    setText('ev-gauge-power', Math.round(simulation.power || 0));
}

async function sendSimulationTelemetry() {
    // Fault Injector uses its existing dedicated path; don't let it compete with EV simulation.
    if (activeFaultMode !== 'none') return;
    var payload = {
        source: 'simulation',
        temperature: Number(simulation.temperature.toFixed(2)),
        voltage: Number(simulation.voltage.toFixed(2)),
        current: Number(simulation.current.toFixed(2)),
        power: Number(simulation.power.toFixed(2)),
        soc: Number(simulation.soc.toFixed(2)),
        cell_voltage: Number(simulation.cellVoltage.toFixed(4)),
        pack_voltage: Number(simulation.voltage.toFixed(2)),
        cell_delta_t: Number(simulation.cellDeltaT.toFixed(2))
    };
    try {
        await fetch('/update_sensor_data', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(payload)
        });
    } catch(e) {
        console.warn('EV simulation telemetry:', e.message);
    }
}

function injectFault(mode) {
    activeFaultMode = mode;
    ['none','overheat','overvolt','overcurrent'].forEach(function(m) {
        var b = document.getElementById('btn-fault-' + m);
        if (b) b.className = 'flex-1 rounded py-1 text-[11px] font-semibold uppercase tracking-wider transition ' + (m === mode ? 'bg-primary text-gray-900' : 'text-gray-400 hover:text-white');
    });

    if (mode === 'overheat') startBuzzerAlarm('overheat', true);
    else if (mode === 'overvolt') startBuzzerAlarm('overvolt', true);
    else if (mode === 'overcurrent') startBuzzerAlarm('overcurrent', true);
    else stopBuzzerSound();

    if (simulationInterval) { clearInterval(simulationInterval); simulationInterval = null; }
    if (mode !== 'none') {
        runFaultSimulationTick();
        simulationInterval = setInterval(runFaultSimulationTick, 1000);
    } else {
        updateSimulation(false);
    }
}

async function runFaultSimulationTick() {
    var temp = sliderTemp + sliderLoad * 15;
    var voltage = 380 + (1 - sliderLoad) * 20;
    var current = 80 + sliderLoad * 160;
    if (activeFaultMode === 'overheat') temp = 62;
    if (activeFaultMode === 'overvolt') voltage = 435;
    if (activeFaultMode === 'overcurrent') current = 320;
    try {
        await fetch('/update_sensor_data', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                source: 'fault-injector',
                temperature: Number(clamp(temp,20,85).toFixed(2)),
                voltage: Number(clamp(voltage,280,430).toFixed(2)),
                current: Number(clamp(current,0,320).toFixed(2)),
                soc: 50
            })
        });
    } catch(e) { console.warn('Fault tick:', e.message); }
}


// ── FUTURE SCOPE & ADVANCED BMS FEATURES UI CONTROLLER ───────────────────────
let rulChart = null;

function updateFutureScopeUI(fs) {
    if (!fs) return;

    // 1. Multi-Cell Matrix & Balancing (Points 1 & 2)
    if (fs.multicell) {
        var mc = fs.multicell;
        setText('val-cell-delta', mc.delta_mv.toFixed(1) + ' mV');
        setText('val-cell-range', mc.v_min.toFixed(2) + 'V – ' + mc.v_max.toFixed(2) + 'V');
        
        var statusEl = document.getElementById('val-cell-status');
        if (statusEl) {
            statusEl.textContent = mc.status;
            statusEl.className = 'text-xs font-semibold ' + (mc.delta_mv > 30 ? 'text-red-400' : (mc.delta_mv > 15 ? 'text-amber-400' : 'text-emerald-400'));
        }

        // Render 16 Cell Cards
        var grid = document.getElementById('cell-matrix-grid');
        if (grid && mc.cells) {
            grid.innerHTML = mc.cells.map(function(c) {
                var isBleeding = c.balancing_active;
                var cellBg = isBleeding ? 'bg-amber-500/10 border-amber-500/40' : 'bg-black/30 border-border/20';
                var vColor = isBleeding ? 'text-amber-300' : 'text-cyan-300';
                return '<div class="rounded-lg p-2 border ' + cellBg + ' transition-all">' +
                       '<div class="flex items-center justify-between text-[9px] text-gray-400">' +
                           '<span>C' + String(c.cell_id).padStart(2, '0') + '</span>' +
                           '<span class="' + (isBleeding ? 'pulse-dot bg-amber-400 inline-block h-1.5 w-1.5 rounded-full' : 'h-1.5 w-1.5 rounded-full bg-gray-600') + '"></span>' +
                       '</div>' +
                       '<div class="text-xs font-bold ' + vColor + ' mt-0.5">' + c.voltage.toFixed(3) + 'V</div>' +
                       '<div class="flex justify-between text-[9px] text-gray-500 mt-1">' +
                           '<span>' + c.temperature.toFixed(1) + '°C</span>' +
                           '<span>' + c.soh.toFixed(0) + '%</span>' +
                       '</div>' +
                       '</div>';
            }).join('');
        }

        // Highlight active mode button
        ['auto', 'on', 'off'].forEach(function(m) {
            var b = document.getElementById('btn-balance-' + m);
            if (b) {
                var isActive = (fs.balancing_mode || 'auto') === m;
                b.className = 'rounded px-2.5 py-1 text-[10px] font-bold uppercase transition ' +
                            (isActive ? 'bg-primary text-gray-900' : 'text-gray-400 hover:text-white');
            }
        });
    }

    // 2. Advanced SOC Estimation (Point 3)
    if (fs.advanced_soc) {
        var soc = fs.advanced_soc;
        setText('soc-val-linear', soc.linear_ocv_soc.toFixed(1) + '%');
        setText('soc-val-coulomb', soc.coulomb_counting_soc.toFixed(1) + '%');
        setText('soc-val-ekf', soc.ekf_soc.toFixed(1) + '%');
        setText('ekf-gain-k', soc.kalman_gain_k ? soc.kalman_gain_k.toFixed(3) : '0.042');
    }

    // 3. SOH & RUL Degradation Predictive Engine (Points 4 & 5)
    if (fs.rul_prediction) {
        var rul = fs.rul_prediction;
        setText('rul-val-soh', rul.soh_score.toFixed(1) + '%');
        setText('rul-val-cycles', rul.rul_cycles + ' cyc');
        setText('rul-val-km', (rul.projected_km ? (rul.projected_km / 1000).toFixed(0) + 'k' : '--') + ' km');
        setText('rul-val-rint', rul.internal_resistance_growth || '+0.4%');

        // Update RUL Chart
        if (rul.curve_points && Array.isArray(rul.curve_points)) {
            var labels = rul.curve_points.map(function(p) { return p.cycle + ' cyc'; });
            var data = rul.curve_points.map(function(p) { return p.soh; });
            initOrUpdateRulChart(labels, data);
        }
    }

    // 4. Edge AI Anomaly Detector (Point 6)
    if (fs.edge_ai_anomaly) {
        var ai = fs.edge_ai_anomaly;
        setText('ai-anomaly-status', ai.status + ' (' + ai.anomaly_score.toFixed(2) + ')');
        setText('ai-val-thermal', ai.thermal_runaway_risk || '0.2%');
        setText('ai-val-microshort', ai.micro_short_detected ? 'DETECTED!' : 'NONE');
        setText('ai-val-impedance', ai.impedance_anomaly ? 'ALERT!' : 'CLEAR');

        var progress = document.getElementById('ai-anomaly-progress');
        if (progress) {
            progress.style.width = Math.min(100, Math.max(2, ai.anomaly_percent)) + '%';
            progress.className = 'h-full rounded-full transition-all duration-500 ' +
                (ai.anomaly_score > 0.6 ? 'bg-red-500' : (ai.anomaly_score > 0.3 ? 'bg-amber-500' : 'bg-emerald-500'));
        }

        var note = document.getElementById('ai-diagnostic-note');
        if (note && ai.detected_reasons && ai.detected_reasons.length > 0) {
            note.textContent = 'AI Diagnostic: ' + ai.detected_reasons.join(' | ');
        }
    }
}

function initOrUpdateRulChart(labels, dataPoints) {
    var canvas = document.getElementById('rulChart');
    if (!canvas) return;
    
    if (!rulChart) {
        var ctx = canvas.getContext('2d');
        rulChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [{
                    label: 'Projected SOH (%)',
                    data: dataPoints,
                    borderColor: '#00f2fe',
                    backgroundColor: 'rgba(0, 242, 254, 0.1)',
                    borderWidth: 2,
                    tension: 0.3,
                    fill: true,
                    pointRadius: 3,
                    pointBackgroundColor: '#00f2fe'
                }, {
                    label: 'EOL Limit (70%)',
                    data: labels.map(function() { return 70; }),
                    borderColor: 'rgba(239, 68, 68, 0.6)',
                    borderWidth: 1.5,
                    borderDash: [4, 4],
                    pointRadius: 0,
                    fill: false
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    y: { min: 65, max: 100, grid: { color: 'rgba(255,255,255,0.04)' }, ticks: { color: '#64748b', font: { size: 9 } } },
                    x: { grid: { display: false }, ticks: { color: '#64748b', font: { size: 9 } } }
                },
                plugins: {
                    legend: { display: false },
                    tooltip: { backgroundColor: 'rgba(5,8,12,0.9)', titleColor: '#9ca3af', bodyColor: '#f3f4f6' }
                }
            }
        });
    } else {
        rulChart.data.labels = labels;
        rulChart.data.datasets[0].data = dataPoints;
        rulChart.data.datasets[1].data = labels.map(function() { return 70; });
        rulChart.update('none');
    }
}

async function setCellBalancingMode(mode) {
    try {
        var res = await fetch('/api/toggle_cell_balancing', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode: mode })
        });
        if (res.ok) pollSensorData();
    } catch(e) { console.warn('Balancing mode error:', e); }
}

async function triggerAIScan() {
    try {
        var res = await fetch('/api/run_ai_anomaly_scan', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' }
        });
        if (res.ok) {
            var data = await res.json();
            if (data.result) {
                alert('Edge AI Diagnostic Scan Complete!\nStatus: ' + data.result.status + '\nAnomaly Score: ' + data.result.anomaly_percent + '%\nThermal Runaway Risk: ' + data.result.thermal_runaway_risk);
                pollSensorData();
            }
        }
    } catch(e) { console.warn('AI Scan error:', e); }
}

