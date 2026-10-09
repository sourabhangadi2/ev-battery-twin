from app.models import db, Alert, Command, SystemLog

# ── Safety thresholds ─────────────────────────────────────────────────────────
TEMP_LOW_FAN_C    = 40.0    # AUTO cooling ON above this temperature
TEMP_WARNING_C    = 40.0    # High-temperature warning threshold
TEMP_CRITICAL_C   = 45.0    # Critical/action threshold

# Configurable 96S EV pack safety envelope.
V_CRITICAL_HIGH   = 430.0   # Pack over-voltage critical trip (V)
V_CRITICAL_LOW    = 280.0   # Pack under-voltage critical trip (V)
V_WARNING_HIGH   = 420.0
V_WARNING_LOW    = 320.0
I_CRITICAL_HIGH   = 300.0   # 400-V-class simulation over-current trip (A)
I_WARNING_HIGH   = 200.0

# ── Recovery hysteresis ───────────────────────────────────────────────────────
TEMP_RECOVERY_C   = 34.0
V_RECOVERY_LOW    = 300.0
V_RECOVERY_HIGH   = 425.0
I_RECOVERY        = 280.0


# ── Public entry point ────────────────────────────────────────────────────────
def run_self_healing_engine(sensor) -> Command:
    """
    Evaluate sensor readings against safety limits.
    Mutates the singleton Command row and files Alerts / SystemLogs.
    Returns the updated Command instance (already committed).
    """
    cmd = Command.query.first()
    if not cmd:
        cmd = Command(relay=True, fan=False, buzzer=False, reset_alarm=False)
        db.session.add(cmd)

    temp    = sensor.temperature
    voltage = sensor.voltage
    current = sensor.current

    # Dynamic mode detection: single-cell prototype vs 96S multi-cell pack
    is_single_cell = (voltage <= 10.0)
    if is_single_cell:
        temp_warning    = 45.0
        temp_critical   = 55.0
        v_warning_high  = 4.20
        v_critical_high = 4.25
        v_warning_low   = 3.20
        v_critical_low  = 2.80
        i_warning_high  = 4.0
        i_critical_high = 5.0
    else:
        temp_warning    = 40.0
        temp_critical   = 45.0
        v_warning_high  = 420.0
        v_critical_high = 430.0
        v_warning_low   = 320.0
        v_critical_low  = 280.0
        i_warning_high  = 200.0
        i_critical_high = 300.0

    critical_active = False

    # ── 1. Temperature checks ──────────────────────────────────────────────────
    previous_fan = bool(cmd.fan)

    if temp >= temp_critical:
        critical_active = True
        cmd.fan    = True
        cmd.buzzer = True
        cmd.relay  = False
        _alert('Over-Temperature', 'CRITICAL',
               f'CRITICAL: Battery temperature {temp:.1f}°C reached '
               f'{temp_critical}°C. Cooling active. Load ISOLATED.')

    elif temp >= 35.0:
        if temp <= temp_warning:
            _alert('Temperature-Warning', 'WARNING',
                   f'WARNING: Battery temperature {temp:.1f}°C is above the preferred 20–35°C range.')
        if temp > temp_warning:
            cmd.fan = True
            if cmd.fan_mode == 'auto':
                _alert('High-Temperature', 'WARNING',
                       f'WARNING: Battery temperature {temp:.1f}°C exceeded {temp_warning}°C. Cooling fan ON.')
        elif cmd.fan_mode == 'on':
            cmd.fan = True
        elif cmd.fan_mode == 'off':
            cmd.fan = False

    elif temp <= 34.0 and cmd.fan_mode == 'auto':
        cmd.fan = False

    elif cmd.fan_mode == 'on':
        cmd.fan = True
    elif cmd.fan_mode == 'off' and temp < temp_warning:
        cmd.fan = False

    if cmd.fan_mode == 'auto' and previous_fan != bool(cmd.fan):
        if cmd.fan:
            _log('AUTO_COOLING', f'Cooling fan automatically turned ON at {temp:.1f}°C.')
        else:
            _log('AUTO_COOLING', f'Cooling fan automatically turned OFF at {temp:.1f}°C.')

    # ── 2. Voltage checks ──────────────────────────────────────────────────────
    if v_critical_low <= voltage < v_warning_low:
        _alert('Low-Voltage-Warning', 'WARNING',
               f'WARNING: Pack voltage {voltage:.1f}V is below normal range.')
    elif v_critical_high > voltage > v_warning_high:
        _alert('High-Voltage-Warning', 'WARNING',
               f'WARNING: Pack voltage {voltage:.1f}V is above normal range.')

    if voltage > v_critical_high:
        critical_active = True
        cmd.relay  = False
        cmd.buzzer = True
        _alert('Over-Voltage', 'CRITICAL',
               f'CRITICAL: Voltage {voltage:.3f}V exceeded over-charge limit '
               f'{v_critical_high}V. Overcharge protection active. Load ISOLATED.')

    elif voltage < v_critical_low:
        critical_active = True
        cmd.relay  = False
        _alert('Under-Voltage', 'CRITICAL',
               f'CRITICAL: Voltage dropped to {voltage:.3f}V below minimum '
               f'{v_critical_low}V. Deep-discharge protection active. Load ISOLATED.')

    # ── 3. Current / short-circuit check ──────────────────────────────────────
    if i_warning_high < current <= i_critical_high:
        _alert('High-Load-Warning', 'WARNING',
               f'WARNING: Battery current {current:.1f}A is in high-load range.')

    if current > i_critical_high:
        critical_active = True
        cmd.relay  = False
        cmd.buzzer = True
        _alert('Over-Current', 'CRITICAL',
               f'CRITICAL: Current surge {current:.2f}A exceeded limit '
               f'{i_critical_high}A. Short-circuit protection active. Load ISOLATED.')

    # ── 4. Critical override safety latch ─────────────────────────────────────
    # Under any active critical condition, relay must remain open regardless of
    # manual mode settings.
    if critical_active:
        cmd.relay = False

    # ── 5. Recovery check (hysteresis) ────────────────────────────────────────
    in_safe_zone = (
        temp    < TEMP_RECOVERY_C and
        V_RECOVERY_LOW < voltage < V_RECOVERY_HIGH and
        current < I_RECOVERY
    )
    active_alerts = Alert.query.filter_by(resolved=False).all()

    if in_safe_zone and active_alerts and not critical_active:
        for alert in active_alerts:
            alert.resolved = True
        cmd.relay  = True
        cmd.fan    = (temp > TEMP_LOW_FAN_C)
        cmd.buzzer = False
        cmd.reset_alarm = False
        _log('RECOVERY',
             'All parameters returned to nominal operating range. '
             'Relay reconnected. All indicators cleared.')

    # Tidy up: clear stale reset_alarm latch if no active alerts remain
    remaining = Alert.query.filter_by(resolved=False).count()
    if remaining == 0 and cmd.reset_alarm:
        cmd.reset_alarm = False

    # ── 6. Apply manual mode overrides (only when no critical trip) ───────────
    if not critical_active:
        if cmd.relay_mode == 'on':
            cmd.relay = True
        elif cmd.relay_mode == 'off':
            cmd.relay = False

        if cmd.fan_mode == 'on':
            cmd.fan = True
        elif cmd.fan_mode == 'off' and temp <= TEMP_WARNING_C:
            cmd.fan = False
        # At/above the warning threshold, automatic protection has priority.
        elif temp > TEMP_WARNING_C:
            cmd.fan = True

    # ── Persist ───────────────────────────────────────────────────────────────
    try:
        db.session.commit()
    except Exception as exc:
        db.session.rollback()
        print(f'[Healing] Commit error: {exc}')

    return cmd


# ── Internal helpers ──────────────────────────────────────────────────────────
def _alert(alert_type: str, severity: str, message: str) -> None:
    """Create alert only if an identical unresolved one does not already exist."""
    exists = Alert.query.filter_by(
        alert_type=alert_type, severity=severity, resolved=False
    ).first()
    if not exists:
        db.session.add(Alert(alert_type=alert_type,
                             severity=severity,
                             message=message,
                             resolved=False))
        _log('HEALING_ACTION', message)


def _log(event: str, description: str) -> None:
    """Append a system log entry, skipping exact consecutive duplicates."""
    last = SystemLog.query.order_by(SystemLog.id.desc()).first()
    if last and last.event == event and last.description == description:
        return
    db.session.add(SystemLog(event=event, description=description))
