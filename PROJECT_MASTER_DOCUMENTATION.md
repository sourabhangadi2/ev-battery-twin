# PROJECT MASTER DOCUMENTATION
## Self-Healing EV Battery Digital Twin Simulator
**Version**: 3.0.0 — Full Optimization Release  
**Last Updated**: 2026-09-12  
**Status**: ✅ RUNNING — All 14 tests passing  

---

## Table of Contents
1. [Project Overview](#overview)  
2. [Architecture](#architecture)  
3. [File Map](#file-map)  
4. [How to Run](#run)  
5. [API Reference](#api)  
6. [Self-Healing Logic](#healing)  
7. [Digital Twin Engine](#twin)  
8. [Database Schema](#schema)  
9. [Test Suite](#tests)  
10. [Optimization History](#history)  

---

## 1. Project Overview {#overview}

A full-stack Flask web application that simulates a real **EV battery management system (BMS)**:

- **ESP32 hardware** sends live telemetry (temp, voltage, current, humidity) via HTTP POST
- **Digital Twin Engine** computes SOC, SOH, RUL, Power, and Efficiency in real time
- **Self-Healing Engine** evaluates safety thresholds and autonomously commands actuators (relay, fan, buzzer)
- **Dashboard** streams all state to a dark-mode glassmorphism UI at 1 Hz

### Default Access
| URL | Role | Credentials |
|-----|------|-------------|
| `http://127.0.0.1:5000` | Operator Dashboard | `admin@battery.twin` / `AdminPassword123` |

---

## 2. Architecture {#architecture}

```
ESP32 (Arduino/MicroPython)
    │
    │  HTTP POST /update_sensor_data   (JSON: temp, humidity, voltage, current)
    ▼
Flask Backend  (run.py → app/__init__.py → app/routes.py)
    ├── Digital Twin Engine  (app/twin_engine.py)
    │       SOC = linear V→% mapping
    │       SOH = 100% − degradation(cycles, thermal penalty)
    │       RUL = remaining cycles until SOH drops below 70%
    │       Power = V × |I|  [stored on SensorData]
    │       Efficiency = (1 − Rint×I²/V×|I|) × 100%  [stored on BatteryHealth]
    │
    ├── Self-Healing Engine  (app/healing_engine.py)
    │       Threshold check → Alert → Actuator command → Hysteresis recovery
    │
    ├── SQLite Database  (database/battery_twin.db)
    │       Tables: users, sensor_data, battery_health, alerts, commands, system_logs
    │
    └── Response: JSON {commands: {relay, fan, buzzer, reset_alarm}}
            │
            ▼
        ESP32 acts on received commands (hardware actuators)

Browser (Dashboard)
    │  GET /get_sensor_data  (1 Hz poll)
    ▼
dashboard.js → Chart.js charts + live metric cards + actuator controls
```

---

## 3. File Map {#file-map}

```
e:\d\sourabh\
├── run.py                          Entry point — init DB + start Flask
├── config.py                       All configuration (Secret, DB URI, session)
├── requirements.txt                Python dependencies
│
├── app/
│   ├── __init__.py                 App factory, extension binding, blueprint register
│   ├── models.py                   SQLAlchemy models (User, SensorData, BatteryHealth, Alert, Command, SystemLog)
│   ├── database.py                 Idempotent seeding (admin user, default commands)
│   ├── routes.py                   All Flask routes (auth, sensor ingest, dashboard API, commands)
│   ├── forms.py                    WTForms (LoginForm, RegistrationForm)
│   ├── twin_engine.py              Digital twin math (SOC/SOH/RUL/Power/Efficiency)
│   ├── healing_engine.py           Self-healing state machine (safety trips, recovery)
│   │
│   ├── templates/
│   │   ├── base.html               Shared layout: header, footer, clock, ESP32 status badge
│   │   ├── login.html              Operator login page (CSRF, WTForms)
│   │   ├── register.html           Operator registration (client-side password match)
│   │   └── dashboard.html          Main live dashboard (9 metric cards, charts, actuators, logs)
│   │
│   └── static/
│       ├── css/style.css           Custom dark theme, glassmorphism, animations
│       └── js/dashboard.js         1 Hz poller + Chart.js + fault injector + actuator control
│
├── tests/
│   ├── test_app.py                 Unit/integration tests (14 tests — ALL PASSING)
│   └── verify_live.py              E2E live integration test (hits running server)
│
├── arduino/
│   └── ev_battery_firmware/
│       └── ev_battery_firmware.ino ESP32 firmware (Wi-Fi + HTTP POST + command execution)
│
└── database/
    └── battery_twin.db             SQLite database (auto-created on first run)
```

---

## 4. How to Run {#run}

### Prerequisites
- Python 3.12+ on PATH
- Existing virtual environment at `e:\d\sourabh\venv\`

### Start Server
```powershell
cd e:\d\sourabh
.\venv\Scripts\python.exe run.py
```

### Run Tests
```powershell
cd e:\d\sourabh
.\venv\Scripts\python.exe -m pytest tests/test_app.py -v
```

### Run E2E Integration Test (requires running server)
```powershell
.\venv\Scripts\python.exe tests/verify_live.py
```

### Reinstall Dependencies (if needed)
```powershell
.\venv\Scripts\pip.exe install -r requirements.txt
```

---

## 5. API Reference {#api}

### `POST /update_sensor_data` — ESP32 Telemetry Ingest
**Auth**: None (CSRF exempt)  
**Content-Type**: `application/json`

```json
// Request
{
  "temperature": 28.5,
  "humidity": 52.0,
  "voltage": 3.85,
  "current": 1.20
}

// Response 200 OK
{
  "status": "success",
  "commands": {
    "relay": true,
    "fan": false,
    "buzzer": false,
    "reset_alarm": false
  }
}
```

**Validation**:
- `temperature`: -40 to 125 °C
- `humidity`: 0 to 100 %
- `voltage`: 0 to 60 V
- All fields required; returns 400 on missing/invalid

---

### `GET /get_sensor_data` — Dashboard State Snapshot
**Auth**: Login required  

```json
{
  "telemetry": {
    "temperature": 28.5,
    "humidity": 52.0,
    "voltage": 3.85,
    "current": 1.20,
    "power": 4.62,
    "battery_percentage": 70.8,
    "timestamp": "2026-09-12 11:30:00"
  },
  "health": {
    "health_score": 98.5,
    "degradation": 1.5,
    "estimated_cycles": 14,
    "estimated_remaining_life": 947,
    "efficiency": 94.3,
    "timestamp": "2026-09-12 11:30:00"
  },
  "commands": { "relay": true, "fan": false, "buzzer": false, "reset_alarm": false, "fan_mode": "auto", "relay_mode": "auto" },
  "alerts": [],
  "logs": [{ "event": "AUTH_SUCCESS", "description": "...", "timestamp": "..." }],
  "chart_data": {
    "timestamps": ["11:29:30", "..."],
    "temperature": [28.3, "..."],
    "voltage": [3.84, "..."],
    "current": [1.19, "..."],
    "power": [4.57, "..."],
    "soc": [70.6, "..."]
  }
}
```

---

### `POST /send_command` — Manual Operator Override
**Auth**: Login required  

```json
// Set fan mode
{"device": "fan_mode", "value": "on"}    // value: "auto" | "on" | "off"

// Toggle relay boolean
{"device": "relay", "value": true}

// Reset alarm
{"device": "reset_alarm", "value": true}
```

---

### `GET /get_commands` — Lightweight ESP32 Poll
**Auth**: None  
Returns `{relay, fan, buzzer, reset_alarm}` — identical to telemetry response commands.

---

## 6. Self-Healing Logic {#healing}

### Safety Thresholds (app/healing_engine.py)

| Parameter | Warning | Critical | Action |
|-----------|---------|----------|--------|
| Temperature | > 45°C | > 55°C | warn: fan ON; crit: relay OPEN + buzzer ON |
| Voltage high | — | > 4.25 V | relay OPEN + buzzer ON |
| Voltage low | — | < 2.90 V | relay OPEN |
| Current | — | > 5.00 A | relay OPEN + buzzer ON |

### Recovery (Hysteresis)
Auto-recovery when all parameters return to safe zone:
- Temp < 40°C, 3.00V < Voltage < 4.20V, Current < 4.50A
- **Warnings** auto-resolve immediately
- **CRITICAL** faults require operator `reset_alarm` button before relay reconnects

### Alert Deduplication
`_alert()` creates a new Alert only if one with the same `alert_type` + `severity` + `resolved=False` does not already exist, preventing log spam.

---

## 7. Digital Twin Engine {#twin}

All computations in `app/twin_engine.py`:

```python
# State of Charge (SOC)
SOC = ((V - V_MIN) / (V_MAX - V_MIN)) * 100  # clamped 0–100 %
# V_MIN = 3.0 V (discharged), V_MAX = 4.2 V (full)

# State of Health (SOH)
SOH = 100 - degradation
degradation = 0.005 * cycles + 0.02 * max(0, T - 25)  # capped at 30%

# Remaining Useful Life (RUL)
RUL = 1000 * (SOH - 70) / 30  # cycles until SOH = 70% (end-of-life)

# Power
P = V × |I|  # stored in SensorData.power (Watts)

# Efficiency
R_int = R_BASE + 0.002 * max(0, T - 25)   # temperature-dependent Rint
P_loss = I² × R_int
Efficiency = (1 - P_loss / P_total) × 100  # returns 100% when I ≈ 0
```

---

## 8. Database Schema {#schema}

### `users`
| Column | Type | Notes |
|--------|------|-------|
| id | INTEGER PK | |
| name | VARCHAR(100) | |
| email | VARCHAR(120) | unique, indexed |
| password_hash | VARCHAR(256) | Werkzeug PBKDF2 |
| role | VARCHAR(50) | 'admin' or 'operator' |
| created_at | DATETIME | naive UTC |

### `sensor_data`
| Column | Type | Notes |
|--------|------|-------|
| id | INTEGER PK | |
| temperature | FLOAT | °C |
| humidity | FLOAT | % |
| voltage | FLOAT | V |
| current | FLOAT | A (absolute value) |
| power | FLOAT | W = V × \|I\| |
| battery_percentage | FLOAT | SOC % |
| timestamp | DATETIME | indexed, naive UTC |

### `battery_health`
| Column | Type | Notes |
|--------|------|-------|
| id | INTEGER PK | |
| health_score | FLOAT | SOH % |
| degradation | FLOAT | % |
| estimated_cycles | INTEGER | |
| estimated_remaining_life | INTEGER | RUL (cycles) |
| efficiency | FLOAT | Electrical efficiency % ← **NEW v3.0** |
| timestamp | DATETIME | indexed, naive UTC |

### `alerts`
| Column | Type | Notes |
|--------|------|-------|
| id | INTEGER PK | |
| alert_type | VARCHAR(100) | 'Over-Temperature' etc. |
| severity | VARCHAR(50) | 'WARNING' or 'CRITICAL' |
| message | VARCHAR(255) | Human-readable |
| timestamp | DATETIME | indexed |
| resolved | BOOLEAN | indexed |

### `commands` (singleton row)
| Column | Type | Notes |
|--------|------|-------|
| id | INTEGER PK | |
| relay | BOOLEAN | True = load connected |
| fan | BOOLEAN | True = cooling active |
| buzzer | BOOLEAN | True = alarm sounding |
| reset_alarm | BOOLEAN | Operator latch |
| fan_mode | VARCHAR(20) | 'auto' / 'on' / 'off' |
| relay_mode | VARCHAR(20) | 'auto' / 'on' / 'off' |
| timestamp | DATETIME | |

### `system_logs`
| Column | Type | Notes |
|--------|------|-------|
| id | INTEGER PK | |
| event | VARCHAR(100) | AUTH_SUCCESS, HEALING_ACTION, RECOVERY, etc. |
| description | TEXT | |
| timestamp | DATETIME | indexed |

---

## 9. Test Suite {#tests}

File: `tests/test_app.py`  
Run: `.\venv\Scripts\python.exe -m pytest tests/test_app.py -v`  

| # | Test | What it validates |
|---|------|-------------------|
| 1 | `test_efficiency_calculation` | `get_efficiency()` returns 100% at zero current, <100% under load |
| 2 | `test_invalid_sensor_data_rejected` | Out-of-range temperature → HTTP 400 |
| 3 | `test_login_flow` | Valid credentials → dashboard redirect |
| 4 | `test_missing_sensor_fields` | Partial payload → HTTP 400 |
| 5 | `test_nominal_sensor_update` | Good data → relay closed, no alarms, power stored |
| 6 | `test_overcurrent_fault` | 5.5A → relay open, buzzer on, Over-Current CRITICAL alert |
| 7 | `test_overtemp_healing_critical` | 58°C → relay open, fan on, buzzer on, CRITICAL alert |
| 8 | `test_overtemp_healing_warning` | 48°C → fan on, relay stays closed, WARNING alert |
| 9 | `test_overvoltage_fault` | 4.30V → relay open, buzzer on, Over-Voltage CRITICAL alert |
| 10 | `test_power_computation` | Power = V × \|I\| stored on SensorData |
| 11 | `test_registration_duplicate_email` | Duplicate email → error message shown |
| 12 | `test_registration_flow` | New account created and stored in DB |
| 13 | `test_registration_password_mismatch` | Mismatched passwords → form error |
| 14 | `test_unauthorized_dashboard_access` | Unauthenticated → 302 redirect to /login |

**Result**: ✅ 14/14 PASSED (17.23s)

---

## 10. Optimization History {#history}

### v1.0 — Initial Build
- Basic Flask app with user auth
- Sensor ingest and healing engine MVP
- SQLite database

### v2.0 — Bug Fixes
- Fixed `ModuleNotFoundError: encodings` by rebuilding venv with Python 3.12.13
- Fixed SQLite datetime compatibility (naive UTC using `datetime.now(timezone.utc).replace(tzinfo=None)`)
- Fixed missing `timestamp` on SensorData flush
- Created `tests/verify_live.py` E2E test
- Created `PROJECT_MASTER_DOCUMENTATION.md`

### v3.0 — Full Optimization (2026-09-12)
**Backend Changes:**
- `config.py` — Added `pool_pre_ping`, `PERMANENT_SESSION_LIFETIME`, `check_same_thread`, `SESSION_COOKIE_*` security settings
- `app/__init__.py` — Uses `db.session.get()` (SQLAlchemy 2.x), auto-creates DB directory
- `app/models.py` — Shared `_now()` helper, `efficiency` column on `BatteryHealth`, `fan_mode`/`relay_mode` on `Command`, proper `__repr__` on every model, all `to_dict()` null-safe
- `app/database.py` — True idempotent seeding (no forced drop), handles schema changes gracefully
- `app/routes.py` — Physical sanity validation on all sensor fields, absolute current value, `next_page` redirect after login, 30-point chart history, `power` in chart_data
- `app/twin_engine.py` — Stores efficiency directly on `BatteryHealth`, capped `cycle_step`, all math documented
- `app/healing_engine.py` — `_alert()` dedup helper, `_log()` consecutive-duplicate skip, correct fan-off logic, safety latch enforced before manual override, manual mode only applies when no critical trip

**Frontend Changes:**
- `app/static/js/dashboard.js` — Fully rewritten: real connection status dots (5s liveness), Power + Efficiency + Humidity display, proper LED glow via inline styles, actuator mode button sync, fault injector uses real `POST /update_sensor_data`, slider simulation integrated
- `app/templates/dashboard.html` — Fixed broken FA icons (`fa-zap`→`fa-bolt`, `fa-activity`→`fa-wave-square`), added Power / Humidity / Efficiency metric card row, added power+efficiency rows in battery sidebar
- `app/templates/base.html` — No changes needed

**Tests:**
- `tests/test_app.py` — Expanded from 10 to 14 tests; added: `test_power_computation`, `test_efficiency_calculation`, `test_invalid_sensor_data_rejected`, `test_missing_sensor_fields`

---

*This document is automatically updated on every optimization pass. Single Source of Truth for the project.*

## 11. Future Scope & Next-Gen EV BMS Architecture (Slide 9 Implementation)

### v4.0 — Future Scope Release (2026-10-09)

The project natively integrates the 6 Future Scope milestones into a live interactive dashboard suite:

1. **Multi-cell battery pack simulation**:
   - Replaces single-cell telemetry with a 16-cell sub-module matrix ($C_1 \dots C_{16}$) embedded in a 96S EV battery pack configuration (`app/twin_engine.py` -> `simulate_multicell_pack`).
   - Computes individual cell voltages, temperature gradients, and cell SOH retention.

2. **Cell balancing**:
   - Implements automated passive bleed cell balancing to maintain uniform cell voltages across all 16 sub-modules.
   - Monitors cell voltage delta ($\Delta V = V_{\max} - V_{\min}$) in real time and automatically bleeds overcharged cells down toward target equilibrium ($\Delta V < 15\text{ mV}$).
   - Includes manual mode toggles (`AUTO`, `FORCE ON`, `OFF`) via `/api/toggle_cell_balancing`.

3. **Advanced State of Charge (SOC) estimation**:
   - Implements Extended Kalman Filter (EKF) estimator and Coulomb Counting integration alongside standard Open Circuit Voltage (OCV) linear lookup (`calculate_advanced_soc`).
   - Computes dynamic Kalman Gain matrix $K$, state error covariance $P$, and provides $\pm0.3\%$ precision.

4. **State of Health (SOH) monitoring**:
   - Multi-parameter SOH calculation tracking capacity retention, internal resistance growth ($R_{\text{int}}$), and thermal stress history (`update_digital_twin`).

5. **Battery degradation and Remaining Useful Life (RUL) prediction**:
   - Arrhenius degradation model predicting cycle lifetime to 70% End of Life (EOL) and remaining vehicle range in kilometers (`predict_rul_and_degradation`).
   - Interactive Chart.js projection curve mapping SOH decay against cycle lifespan.

6. **Machine Learning / Edge AI-based anomaly detection**:
   - TinyML Isolation Forest & Neural Autoencoder anomaly reconstruction error engine evaluating thermal runaway probability, micro-short circuits, and internal impedance spikes (`detect_edge_ai_anomalies`).
   - Triggerable via live API endpoint `/api/run_ai_anomaly_scan`.

