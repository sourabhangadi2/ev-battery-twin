import socket
from datetime import datetime, timezone
# pyrefly: ignore [missing-import]
from flask import Blueprint, render_template, redirect, url_for, flash, request, jsonify, send_from_directory
from flask_login import login_user, logout_user, login_required, current_user

from app import csrf
from app.models import (
    db,
    User,
    SensorData,
    BatteryHealth,
    Alert,
    Command,
    SystemLog
)

from app.forms import LoginForm, RegistrationForm
from app.twin_engine import (
    update_digital_twin,
    simulate_multicell_pack,
    calculate_advanced_soc,
    predict_rul_and_degradation,
    detect_edge_ai_anomalies
)
from app.healing_engine import run_self_healing_engine

_current_balancing_mode = 'auto'


bp = Blueprint('main', __name__)


# ============================================================
# VALID DEVICES / MODES
# ============================================================

VALID_DEVICES = {
    'relay_mode',
    'fan_mode',
    'buzzer',
    'reset_alarm',
    'relay',
    'fan'
}

VALID_MODES = {
    'auto',
    'on',
    'off'
}


# ============================================================
# TIME
# ============================================================

def _now():
    return datetime.now(timezone.utc).replace(tzinfo=None)


# ============================================================
# AUTH ROUTES
# ============================================================

@bp.route('/')
def index():
    return redirect(
        url_for('main.dashboard')
        if current_user.is_authenticated
        else url_for('main.login')
    )


@bp.route('/manifest.json')
def manifest():
    return send_from_directory('static', 'manifest.json', mimetype='application/json')


@bp.route('/sw.js')
def service_worker():
    return send_from_directory('static', 'sw.js', mimetype='application/javascript')


@bp.route('/check_auth_status')
def check_auth_status():
    """Returns whether any non-admin operator or custom user has registered yet."""
    user_count = User.query.count()
    # If only 1 user exists (seeded admin), or 0, consider first-time
    has_registered_user = user_count > 1
    return jsonify(
        has_registered_user=has_registered_user,
        total_users=user_count
    )


@bp.route('/register', methods=['GET', 'POST'])
def register():
    if current_user.is_authenticated:
        return redirect(url_for('main.dashboard'))

    login_form = LoginForm()
    register_form = RegistrationForm()

    if register_form.validate_on_submit():
        email = register_form.email.data.strip().lower()

        if User.query.filter_by(email=email).first():
            register_form.email.errors.append(
                'This email address is already registered. Please sign in.'
            )
            return render_template(
                'login.html',
                login_form=login_form,
                register_form=register_form,
                active_tab='register'
            )

        user = User(
            name=register_form.name.data.strip(),
            email=email,
            role='operator'
        )
        user.set_password(register_form.password.data)

        db.session.add(user)
        db.session.add(
            SystemLog(
                event='AUTH_REGISTER',
                description=f"New operator registered: '{user.name}' ({email}).",
                timestamp=_now()
            )
        )
        db.session.commit()

        flash(
            'Account created successfully! Please sign in with your credentials.',
            'success'
        )
        return redirect(url_for('main.login', registered=1))

    return render_template(
        'login.html',
        login_form=login_form,
        register_form=register_form,
        active_tab='register'
    )


@bp.route('/login', methods=['GET', 'POST'])
def login():
    if current_user.is_authenticated:
        return redirect(url_for('main.dashboard'))

    login_form = LoginForm()
    register_form = RegistrationForm()

    if request.method == 'POST' and login_form.validate_on_submit():
        email = login_form.email.data.strip().lower()
        user = User.query.filter_by(email=email).first()

        if user and user.check_password(login_form.password.data):
            login_user(
                user,
                remember=login_form.remember.data
            )
            db.session.add(
                SystemLog(
                    event='AUTH_SUCCESS',
                    description=f"Operator '{user.name}' ({email}) logged in.",
                    timestamp=_now()
                )
            )
            db.session.commit()

            flash(
                f'Welcome back, {user.name}!',
                'success'
            )
            next_page = request.args.get('next')
            return redirect(next_page or url_for('main.dashboard'))
        else:
            db.session.add(
                SystemLog(
                    event='AUTH_FAILED',
                    description=f"Failed login attempt for: '{login_form.email.data}'.",
                    timestamp=_now()
                )
            )
            db.session.commit()
            flash(
                'Invalid email or password. Please try again.',
                'danger'
            )

    # Check active tab preference
    tab = request.args.get('tab', 'login')

    return render_template(
        'login.html',
        login_form=login_form,
        register_form=register_form,
        active_tab=tab
    )


@bp.route('/logout')
@login_required
def logout():

    name = current_user.name

    logout_user()

    db.session.add(
        SystemLog(
            event='AUTH_LOGOUT',
            description=f"Operator '{name}' logged out.",
            timestamp=_now()
        )
    )

    db.session.commit()

    flash(
        'You have been logged out successfully.',
        'info'
    )

    return redirect(url_for('main.login'))


@bp.route('/dashboard')
@login_required
def dashboard():

    return render_template(
        'dashboard.html',
        name=current_user.name
    )


# ============================================================
# ESP32 → PYTHON SENSOR DATA
# ============================================================

@bp.route('/update_sensor_data', methods=['POST'])
@csrf.exempt
def update_sensor_data():

    """
    Receives REAL sensor data from ESP32.

    ESP32 must send:
        source       = "esp32"
        temperature  = DS18B20 value
        voltage      = INA219 voltage
        current      = INA219 current

    Humidity is no longer required because DS18B20
    does not measure humidity.
    """

    payload = request.get_json(silent=True)

    if not payload:

        return jsonify(
            status='error',
            message='Missing or malformed JSON payload.'
        ), 400


    # --------------------------------------------------------
    # Read sensor values (ESP32 or Dashboard Fault Injector)
    # --------------------------------------------------------

    try:

        temp = float(payload['temperature'])
        voltage = float(payload['voltage'])
        current = float(payload['current'])

    except (KeyError, TypeError, ValueError) as exc:

        return jsonify(
            status='error',
            message=f'Invalid parameter: {exc}'
        ), 400


    # --------------------------------------------------------
    # Humidity
    # --------------------------------------------------------
    # DS18B20 does not measure humidity.
    # Keep the existing database/dashboard structure unchanged.
    # Therefore humidity is stored as 0.0.
    # --------------------------------------------------------

    humidity = 0.0


    # --------------------------------------------------------
    # Physical sanity checks
    # --------------------------------------------------------

    if not (-40 <= temp <= 125):

        return jsonify(
            status='error',
            message='temperature out of physical range.'
        ), 400


    # --------------------------------------------------------
    # EV Digital Twin pack-voltage compatibility
    # --------------------------------------------------------
    # The dashboard now uses a configurable 96S EV pack model.
    # Existing ESP32 firmware may still report one-cell voltage
    # (roughly 3.2–4.2 V). Convert that value to an equivalent
    # 96-series pack voltage while preserving the endpoint.
    #
    # Simulation/fault-injector clients already send pack voltage.
    # --------------------------------------------------------
    source = str(payload.get('source', 'unknown')).lower()
    supplied_cell_voltage = payload.get('cell_voltage')
    supplied_pack_voltage = payload.get('pack_voltage')

    if supplied_pack_voltage is not None:
        try:
            voltage = float(supplied_pack_voltage)
        except (TypeError, ValueError):
            pass
    elif source in ('esp32', 'simulation', 'pack') and 2.5 <= voltage <= 4.5:
        supplied_cell_voltage = voltage
        voltage = voltage * 96.0

    if not ((2.5 <= voltage <= 4.5) or (280 <= voltage <= 430)):
        return jsonify(
            status='error',
            message='voltage out of supported Digital Twin range (2.5–4.5 V cell or 280–430 V pack).'
        ), 400

    if current < 0:
        current = abs(current)

    # --------------------------------------------------------
    # Optional deterministic SOC supplied by the simulation.
    # Real ESP32 telemetry continues to use the Digital Twin
    # voltage-derived fallback in twin_engine.py.
    # --------------------------------------------------------
    supplied_soc = payload.get('soc')
    try:
        supplied_soc = float(supplied_soc) if supplied_soc is not None else None
    except (TypeError, ValueError):
        supplied_soc = None

    # --------------------------------------------------------
    # Create sensor record
    # --------------------------------------------------------
    now = _now()
    sensor = SensorData(
        temperature=round(temp, 2),
        humidity=round(humidity, 2),
        voltage=round(voltage, 3),
        current=round(abs(current), 3),
        power=round(voltage * abs(current), 3),
        battery_percentage=(
            round(max(0.0, min(100.0, supplied_soc)), 2)
            if supplied_soc is not None else 50.0
        ),
        timestamp=now
    )
    sensor._provided_soc = supplied_soc
    sensor._cell_voltage = (
        float(supplied_cell_voltage)
        if supplied_cell_voltage is not None
        else voltage / 96.0
    )


    db.session.add(sensor)

    db.session.flush()


    # --------------------------------------------------------
    # DIGITAL TWIN
    # --------------------------------------------------------

    update_digital_twin(sensor)


    # --------------------------------------------------------
    # SELF-HEALING ENGINE
    # --------------------------------------------------------

    cmd = run_self_healing_engine(sensor)


    # --------------------------------------------------------
    # RETURN COMMANDS TO ESP32
    # --------------------------------------------------------

    return jsonify(

        status='success',

        commands={

            'relay': cmd.relay,

            'fan': cmd.fan,

            'buzzer': cmd.buzzer,

            'reset_alarm': cmd.reset_alarm

        }

    ), 200


# ============================================================
# DASHBOARD SENSOR DATA
# ============================================================

@bp.route('/get_sensor_data', methods=['GET'])
@login_required
def get_sensor_data():

    """
    Dashboard polling endpoint.

    The existing dashboard calls this endpoint every second.
    """

    sensor = (
        SensorData
        .query
        .order_by(SensorData.id.desc())
        .first()
    )


    health = (
        BatteryHealth
        .query
        .order_by(BatteryHealth.id.desc())
        .first()
    )


    cmd = Command.query.first()


    alerts = (
        Alert
        .query
        .filter_by(resolved=False)
        .order_by(Alert.id.desc())
        .all()
    )


    logs = (
        SystemLog
        .query
        .order_by(SystemLog.id.desc())
        .limit(20)
        .all()
    )


    # --------------------------------------------------------
    # Last 30 readings
    # --------------------------------------------------------

    history = (
        SensorData
        .query
        .order_by(SensorData.id.desc())
        .limit(30)
        .all()
    )

    history.reverse()


    # --------------------------------------------------------
    # Latest telemetry
    # --------------------------------------------------------

    telemetry_dict = None


    if sensor:

        telemetry_dict = {

            'temperature':
                sensor.temperature,

            'humidity':
                sensor.humidity,

            'voltage':
                sensor.voltage,

            'pack_voltage':
                sensor.voltage,

            'cell_voltage':
                round(getattr(sensor, '_cell_voltage', sensor.voltage / 96.0), 4),

            'current':
                sensor.current,

            'power':
                sensor.power,

            'battery_percentage':
                round(
                    sensor.battery_percentage,
                    1
                ),

            'timestamp':
                sensor.timestamp.strftime(
                    '%Y-%m-%d %H:%M:%S'
                )
        }


    # --------------------------------------------------------
    # Battery health
    # --------------------------------------------------------

    health_dict = None


    if health:

        health_dict = {

            'health_score':
                health.health_score,

            'degradation':
                health.degradation,

            'estimated_cycles':
                health.estimated_cycles,

            'estimated_remaining_life':
                health.estimated_remaining_life,

            'efficiency':
                health.efficiency,

            'timestamp':
                health.timestamp.strftime(
                    '%Y-%m-%d %H:%M:%S'
                )
        }


    # --------------------------------------------------------
    # FUTURE SCOPE & ADVANCED BMS FEATURES (6 Key Points)
    # --------------------------------------------------------
    current_temp = sensor.temperature if sensor else 25.0
    current_volt = sensor.voltage if sensor else 355.2
    current_curr = sensor.current if sensor else 0.0
    current_soc  = sensor.battery_percentage if sensor else 85.0
    current_soh  = health.health_score if health else 98.5
    current_cycles = health.estimated_cycles if health else 12

    multicell_data = simulate_multicell_pack(
        current_volt, current_curr, current_temp, _current_balancing_mode
    )
    advanced_soc_data = calculate_advanced_soc(
        current_volt, current_curr, current_temp, current_soc
    )
    rul_data = predict_rul_and_degradation(
        current_soh, current_cycles, current_temp
    )
    edge_ai_data = detect_edge_ai_anomalies(
        current_temp, current_volt, current_curr, multicell_data['delta_mv']
    )

    future_scope_dict = {
        'multicell': multicell_data,
        'advanced_soc': advanced_soc_data,
        'rul_prediction': rul_data,
        'edge_ai_anomaly': edge_ai_data,
        'balancing_mode': _current_balancing_mode,
        'points': [
            {
                'id': 1,
                'title': 'Multi-cell battery pack simulation',
                'subtitle': 'Instead of a single-cell prototype',
                'status': 'ACTIVE DEMO',
                'description': 'Simulates 16 individual battery cell modules (C1–C16) with live per-cell voltage, temperature gradient, and health tracking in a 96S pack matrix.'
            },
            {
                'id': 2,
                'title': 'Cell balancing',
                'subtitle': 'For maintaining uniform cell voltage',
                'status': 'ACTIVE DEMO',
                'description': 'Passive/Active cell balancing bleeding current from overcharged cells to keep cell delta (ΔV) under 15 mV.'
            },
            {
                'id': 3,
                'title': 'Advanced State of Charge (SOC) estimation',
                'subtitle': 'Extended Kalman Filter & Coulomb Counting',
                'status': 'ACTIVE DEMO',
                'description': 'Real-time state estimator comparing Open Circuit Voltage (OCV), Coulomb Counting, and Extended Kalman Filter (EKF) with ±0.3% error margin.'
            },
            {
                'id': 4,
                'title': 'State of Health (SOH) monitoring',
                'subtitle': 'Capacity retention & internal resistance',
                'status': 'ACTIVE DEMO',
                'description': 'Real-time SOH tracking analyzing capacity loss, internal resistance growth (Rint), and thermal stress penalties.'
            },
            {
                'id': 5,
                'title': 'Battery degradation and Remaining Useful Life (RUL) prediction',
                'subtitle': 'Cycle decay & km range projection',
                'status': 'ACTIVE DEMO',
                'description': 'Multi-factor Arrhenius degradation model forecasting cycle lifetime to EOL (70% SOH) and remaining vehicle range in km.'
            },
            {
                'id': 6,
                'title': 'Machine Learning / Edge AI-based anomaly detection',
                'subtitle': 'TinyML Isolation Forest & Autoencoder',
                'status': 'ACTIVE DEMO',
                'description': 'Real-time neural network anomaly index detecting thermal runaway probability, micro-short circuits, and impedance spikes.'
            }
        ]
    }

    # --------------------------------------------------------
    # Return JSON to dashboard
    # --------------------------------------------------------

    return jsonify(

        telemetry=telemetry_dict,

        health=health_dict,

        future_scope=future_scope_dict,

        commands=cmd.to_dict()
        if cmd else None,

        alerts=[
            a.to_dict()
            for a in alerts
        ],

        logs=[
            l.to_dict()
            for l in logs
        ],

        chart_data={

            'timestamps': [
                r.timestamp.strftime('%H:%M:%S')
                for r in history
            ],

            'temperature': [
                r.temperature
                for r in history
            ],

            'voltage': [
                r.voltage
                for r in history
            ],

            'current': [
                r.current
                for r in history
            ],

            'power': [
                r.power
                for r in history
            ],

            'soc': [
                r.battery_percentage
                for r in history
            ]
        }

    ), 200


def _get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return '127.0.0.1'


@bp.route('/api/network_info', methods=['GET'])
def network_info():
    ip = _get_local_ip()
    mobile_url = f"http://{ip}:5000"
    qr_code_url = f"https://api.qrserver.com/v1/create-qr-code/?size=220x220&data={mobile_url}&color=00f2fe&bcolor=05080c"
    return jsonify(
        status='success',
        local_ip=ip,
        port=5000,
        mobile_url=mobile_url,
        qr_code_url=qr_code_url
    )


@bp.route('/api/toggle_cell_balancing', methods=['POST'])
@login_required
@csrf.exempt
def toggle_cell_balancing():
    global _current_balancing_mode
    payload = request.get_json(silent=True) or {}
    mode = str(payload.get('mode', 'auto')).lower()
    if mode in ('auto', 'on', 'off'):
        _current_balancing_mode = mode
        db.session.add(SystemLog(
            event='CELL_BALANCING_MODE',
            description=f"Cell balancing mode set to '{mode.upper()}' by operator."
        ))
        db.session.commit()
        return jsonify(status='success', mode=_current_balancing_mode)
    return jsonify(status='error', message='Invalid mode'), 400


@bp.route('/api/run_ai_anomaly_scan', methods=['POST'])
@login_required
@csrf.exempt
def run_ai_anomaly_scan():
    sensor = SensorData.query.order_by(SensorData.id.desc()).first()
    health = BatteryHealth.query.order_by(BatteryHealth.id.desc()).first()
    
    current_temp = sensor.temperature if sensor else 25.0
    current_volt = sensor.voltage if sensor else 355.2
    current_curr = sensor.current if sensor else 0.0
    
    multicell = simulate_multicell_pack(current_volt, current_curr, current_temp, _current_balancing_mode)
    ai_result = detect_edge_ai_anomalies(current_temp, current_volt, current_curr, multicell['delta_mv'])
    
    db.session.add(SystemLog(
        event='EDGE_AI_SCAN',
        description=f"Edge AI Scan completed. Anomaly Index: {ai_result['anomaly_percent']}%. Status: {ai_result['status']}."
    ))
    db.session.commit()
    return jsonify(status='success', result=ai_result)


# ============================================================
# MANUAL COMMAND FROM DASHBOARD
# ============================================================

@bp.route('/send_command', methods=['POST'])
@login_required
@csrf.exempt
def send_command():

    payload = request.get_json(silent=True)

    if not payload:

        return jsonify(
            status='error',
            message='Missing JSON payload.'
        ), 400


    device = payload.get('device')

    value = payload.get('value')


    if device not in VALID_DEVICES:

        return jsonify(

            status='error',

            message=(
                f'Unknown device "{device}". '
                f'Valid: {sorted(VALID_DEVICES)}.'
            )

        ), 400


    cmd = Command.query.first()


    if not cmd:

        cmd = Command(

            relay=True,

            fan=False,

            buzzer=False,

            reset_alarm=False,

            fan_mode='auto',

            relay_mode='auto'
        )

        db.session.add(cmd)


    desc = ''


    # --------------------------------------------------------
    # Relay / Fan mode
    # --------------------------------------------------------

    if device in (
        'relay_mode',
        'fan_mode'
    ):

        if value not in VALID_MODES:

            return jsonify(

                status='error',

                message=(
                    f'Invalid mode "{value}". '
                    f'Valid: {sorted(VALID_MODES)}.'
                )

            ), 400


        if device == 'relay_mode':

            cmd.relay_mode = value

            if value == 'on':
                cmd.relay = True

            if value == 'off':
                cmd.relay = False


        else:

            cmd.fan_mode = value

            if value == 'on':
                cmd.fan = True

            if value == 'off':
                cmd.fan = False


        desc = (
            f'Operator set '
            f'{device} → {value.upper()}.'
        )


    # --------------------------------------------------------
    # Direct command
    # --------------------------------------------------------

    elif device in (
        'relay',
        'fan',
        'buzzer',
        'reset_alarm'
    ):

        if not isinstance(value, bool):

            return jsonify(
                status='error',
                message='Value must be a boolean.'
            ), 400


        setattr(
            cmd,
            device,
            value
        )


        if device == 'reset_alarm':
            Alert.query.filter_by(resolved=False).update({Alert.resolved: True})
            cmd.relay = True
            cmd.buzzer = False
            cmd.reset_alarm = False
            desc = 'Operator muted alarm and reset system alert state.'

        elif device == 'relay':

            cmd.relay_mode = (
                'on'
                if value
                else 'off'
            )


        elif device == 'fan':
            cmd.fan_mode = (
                'on'
                if value
                else 'off'
            )


        desc = (
            f'Operator toggled '
            f'{device} → {value}.'
        )


    db.session.add(

        SystemLog(

            event='COMMAND_MANUAL',

            description=desc,

            timestamp=_now()
        )
    )


    try:

        db.session.commit()

        return jsonify(

            status='success',

            message=(
                f'{device.upper()} updated.'
            )

        ), 200


    except Exception as exc:

        db.session.rollback()

        return jsonify(

            status='error',

            message=str(exc)

        ), 500


# ============================================================
# ESP32 COMMAND POLLING
# ============================================================

@bp.route('/get_commands', methods=['GET'])
@csrf.exempt
def get_commands():

    """
    ESP32 can use this endpoint if required.

    Currently the firmware receives commands directly
    from the /update_sensor_data response.
    """

    cmd = Command.query.first()


    if not cmd:

        cmd = Command(

            relay=True,

            fan=False,

            buzzer=False,

            reset_alarm=False
        )

        db.session.add(cmd)

        db.session.commit()


    return jsonify(

        relay=cmd.relay,

        fan=cmd.fan,

        buzzer=cmd.buzzer,

        reset_alarm=cmd.reset_alarm

    ), 200