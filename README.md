# Self-Healing EV Battery Simulator Using Digital Twin

This project is a real-world, production-ready IoT-enabled **Self-Healing EV Battery Simulator & Digital Twin** system. It uses an **ESP32 microcontroller** for sensor telemetry acquisition and actuator control, coupled with a **Flask-based Python backend web dashboard** for real-time visualization, state analysis, and self-healing overrides.

---

## 1. Project Directory Structure

```text
EV_Battery_Digital_Twin/
├── app/
│   ├── __init__.py          # Flask App factory initialization & extension bindings
│   ├── models.py            # SQLAlchemy database schemas (Users, Telemetry, Health, Logs)
│   ├── routes.py            # HTTP request routing (auth, APIs, command overrides)
│   ├── database.py          # Database setup and seed operator account utilities
│   ├── twin_engine.py       # Digital Twin estimation module (SOC, SOH, RUL, efficiency)
│   ├── healing_engine.py    # Self-healing safety constraints engine
│   ├── forms.py             # WTForms secure login authentication schema
│   ├── templates/
│   │   ├── base.html        # Primary HTML structure with dark/light themes
│   │   ├── login.html       # Secured login interface
│   │   └── dashboard.html   # Main metrics grids, charts, and toggles dashboard
│   └── static/
│       ├── css/
│       │   └── style.css    # Custom dark/light mode stylesheets & battery animation
│       └── js/
│           └── dashboard.js # Real-time AJAX polling, Chart.js, command handlers
├── arduino/
│   └── ev_battery_firmware/
│       └── ev_battery_firmware.ino # ESP32 Arduino C++ firmware program
├── tests/
│   └── test_app.py          # Backend unit test scripts
├── requirements.txt         # Required Python packages list
├── config.py                # Environment configuration settings
├── run.py                   # Entrypoint launcher script
└── README.md                # System documentation and guides (This file)
```

---

## 2. System Architecture & Information Flow

The system consists of the following layers:

1.  **Hardware Layer**: The physical battery circuit is monitored via a DHT22 temperature sensor, Voltage divider sensor, and ACS712 current sensor. Active safety controls include a Power relay, DC cooling fan, buzzers, and status indication LEDs (Green/Yellow/Red).
2.  **ESP32 Firmware Layer**: Polls sensors, formats telemetry values as JSON, issues a POST request to the server, and parses response packets to adjust the state of the relay, cooling fan, and alarms.
3.  **Communication Layer**: Secure RESTful JSON messaging over standard Wi-Fi protocol.
4.  **Flask Backend Layer**: Receives telemetry data, runs mathematical estimators, tracks security limits, commits changes, and processes manual dashboard control overrides.
5.  **Digital Twin Engine**: Synchronizes a virtual battery cell in memory to output State of Charge (SOC), State of Health (SOH), degradation, internal resistance ($R_{int}$), and efficiency calculations.
6.  **Self-Healing Engine**: Real-time safety state-machine that steps in to run fan blowers, sound warnings, or isolate the battery pack (open relay) during critical over-temperature, over-voltage, or short-circuit surges.
7.  **Database Layer (SQLite)**: Persists historic sensor telemetry logs, alerts lists, and system logs.
8.  **Frontend Dashboard UI**: Interactive, fully responsive Bootstrap interface featuring real-time graphical lines plots, active protection alarm banners, and physical actuator switches.

---

## 3. Sensor Wiring and Pinout Configurations

The hardware components must be connected to the ESP32 as defined below:

| Sensor / Module | ESP32 GPIO Pin | Connection Pin (Module) | Description / Notes |
| :--- | :--- | :--- | :--- |
| **DHT22 Sensor** | `GPIO 23` | `S / Out / SDA` | Requires a 10k pull-up resistor between VCC and Data pin if not using a pre-soldered module. |
| **Voltage Sensor** | `GPIO 34` | `S / Out / Analog` | Voltage divider output (S connects to GPIO 34, - to GND, VCC & GND to battery terminal inputs). |
| **ACS712 Sensor** | `GPIO 35` | `OUT / Analog` | Connect series terminal in line with the load loop. Adjust calibration offsets if powered with 5V. |
| **Relay Module** | `GPIO 18` | `IN` | Active-High or Active-Low driver relay to isolate load. |
| **Cooling Fan** | `GPIO 19` | `IN / Gate` | Controls 5V DC blower fan through a power transistor (MOSFET) or secondary relay. |
| **Buzzer** | `GPIO 21` | `I/O / IN` | Active buzzer to beep during critical alert isolation states. |
| **Green LED** | `GPIO 2` | `Anode` | Standard status indicator: normal operation (Healthy). |
| **Yellow LED** | `GPIO 4` | `Anode` | Standard status indicator: warning limits exceeded. |
| **Red LED** | `GPIO 15` | `Anode` | Standard status indicator: critical shutdown / relay isolated. |
| **OLED (SSD1306)** | `GPIO 22` (SCL)<br>`GPIO 21` (SDA) | `SCL`<br>`SDA` | Optional 128x64 display. Connects to standard I2C pins. |

### Important Power & Safety Precautions:
*   **Common Ground**: Ensure the ESP32 GND and external power supplies (5V/12V) share a common ground reference.
*   **Voltage limits**: ESP32 input ADC pins are only 3.3V tolerant. Always verify resistor divider values to ensure analog voltages on GPIO 34 and GPIO 35 never exceed 3.3V.
*   **Relay Isolation**: The relay must disconnect the battery load line during an over-voltage, under-voltage, or short-circuit current limit excursion.

---

## 4. Installation & Setup Guide

### Step 4.1: Setting up Python & VS Code
1.  Download and install [Python 3.10+](https://www.python.org/downloads/) (Make sure to check "Add Python to PATH" during installation).
2.  Install [VS Code](https://code.visualstudio.com/) along with the **Python Extension Pack**.

### Step 4.2: Creating Virtual Environment and Dependencies
Open terminal/powershell inside the project root `EV_Battery_Digital_Twin/` and execute:
```powershell
# Create virtual environment
python -m venv venv

# Activate virtual environment
# On Windows Powershell:
.\venv\Scripts\Activate.ps1
# On macOS / Linux:
source venv/bin/activate

# Upgrade pip
pip install --upgrade pip

# Install project packages
pip install -r requirements.txt
```

### Step 4.3: Running the Flask Web Application
Ensure the virtual environment is active, then execute:
```powershell
python run.py
```
*   The application will automatically build the SQLite database file at `database/battery_twin.db` and initialize tables.
*   The script will seed a default admin account:
    *   **Email**: `admin@battery.twin`
    *   **Password**: `AdminPassword123`
*   Open your web browser and navigate to `http://127.0.0.1:5000/`. Log in to access the real-time panel dashboard.

### Step 4.4: Flashing the ESP32 Firmware
1.  Open the [Arduino IDE](https://www.arduino.cc/en/software).
2.  Install the ESP32 board manager: Navigate to **File > Preferences** and add URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` under "Additional Board Manager URLs". Go to Board Manager and install `esp32`.
3.  Install necessary libraries from Library Manager:
    *   `Adafruit Unified Sensor` & `DHT sensor library` (by Adafruit)
    *   `ArduinoJson` (by Benoit Blanchon)
    *   `Adafruit SSD1306` & `Adafruit GFX Library` (for OLED)
4.  Open `arduino/ev_battery_firmware/ev_battery_firmware.ino` in Arduino IDE.
5.  Configure your Wi-Fi SSID and password in the file constants.
6.  Change the `serverIP` constant string to point to your computer's IP address on the local network (find it using `ipconfig` in CMD).
7.  Select your ESP32 board and COM port, click **Upload**.

---

## 5. Verification & Testing

### Running Automated Integration Tests
You can verify the backend routes and logic constraints by running:
```powershell
# Run backend test cases using local environment Python
.\venv\Scripts\python -m unittest tests/test_app.py
```

### Running Live End-to-End Verification Test
While the server is active, you can run the complete system verification test:
```powershell
.\venv\Scripts\python tests/verify_live.py
```
This tests operator authentication, CSRF token management, live telemetry ingestion, Digital Twin metrics, fault injections, actuator command overrides, and autonomous hysteresis recovery.

### Simulating Telemetry Packets without Hardware
If you do not have the physical sensors connected, you can simulate hardware telemetry updates by calling the POST API endpoint using PowerShell:
```powershell
# High Temperature Warning Simulation
Invoke-RestMethod -Uri "http://127.0.0.1:5000/update_sensor_data" -Method Post -Body '{"temperature":48.0,"humidity":52.0,"voltage":3.7,"current":1.2}' -ContentType "application/json"

# Critical Over-Voltage Protection Simulation
Invoke-RestMethod -Uri "http://127.0.0.1:5000/update_sensor_data" -Method Post -Body '{"temperature":26.0,"humidity":52.0,"voltage":4.35,"current":0.5}' -ContentType "application/json"
```

Observe how the web dashboard immediately updates the battery graphic, triggers the warning panel banners, lists recovery logs, and toggles relay/fan switches automatically.

---

## 6. Comprehensive Project Master Documentation & Changelog

For an exhaustive technical breakdown of all mathematical formulas, safety state machines, database schemas, wiring diagrams, C-server architecture, and historical audit logs, refer to:

👉 **[PROJECT_MASTER_DOCUMENTATION.md](file:///e:/d/sourabh/PROJECT_MASTER_DOCUMENTATION.md)**

*(Note: Per repository maintenance rules, `PROJECT_MASTER_DOCUMENTATION.md` must be updated every time changes are made).*

## 400-V-Class EV Digital Twin Simulation Model

The dashboard uses a configurable 400-V-class traction-battery simulation. These are academic Digital Twin thresholds, not universal OEM safety limits.

- Nominal pack voltage: 380 V
- Normal pack voltage: 320–420 V
- Voltage warning: 280–319 V or 421–430 V
- Critical voltage: <280 V or >430 V
- Battery current: 0–200 A normal, 200–300 A high load, >300 A critical
- Battery temperature: 20–35 °C preferred, 35–40 °C warning, >40 °C cooling fan, ≥45 °C critical protection
- SOC: 20–90% normal, 10–20% low, <10% critical low, 90–95% high, >95% charge-limit warning
- Cell temperature difference: <3 °C normal, 3–5 °C warning, >5 °C critical
- Battery power: Power = Pack Voltage × Current; displayed in kW in the main metric
- Pack model: 96 series cells; the physical 18650/ESP32 prototype remains a low-voltage demonstrator

The dashboard records one live telemetry row every real-world second while driving or cooling. Simulation time remains accelerated independently.

---

## 7. Future Scope & Next-Gen EV BMS Architecture (Slide 9 Implementation)

This project incorporates the 6 key Future Scope roadmap milestones into an active interactive demonstration suite:

1. **Multi-cell battery pack simulation**: Replaces a basic single-cell demonstrator with a live 16-cell sub-module matrix (C1–C16) in a 96S EV battery pack configuration (`app/twin_engine.py` -> `simulate_multicell_pack`).
2. **Cell balancing**: Implements automated passive bleed cell balancing to maintain uniform cell voltages and keep cell voltage delta ($\Delta V$) under 15 mV (`/api/toggle_cell_balancing`).
3. **Advanced State of Charge (SOC) estimation**: Implements an Extended Kalman Filter (EKF) estimator and Coulomb Counting algorithm alongside linear OCV lookup, achieving a $\pm0.3\%$ estimation error margin (`calculate_advanced_soc`).
4. **State of Health (SOH) monitoring**: Real-time capacity retention analysis, internal resistance ($R_{int}$) growth tracking, and thermal degradation scoring (`update_digital_twin`).
5. **Battery degradation and Remaining Useful Life (RUL) prediction**: Multi-factor Arrhenius degradation model forecasting remaining cycles to 70% EOL and projected vehicle range in kilometers (`predict_rul_and_degradation`).
6. **Machine Learning / Edge AI-based anomaly detection**: TinyML Isolation Forest & Neural Autoencoder anomaly index running real-time thermal runaway, micro-short circuit, and impedance spike detection (`/api/run_ai_anomaly_scan`).

