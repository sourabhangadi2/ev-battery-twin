from datetime import datetime, timezone
from app.models import db, User, Command, SensorData, BatteryHealth, SystemLog


def _now():
    return datetime.now(timezone.utc).replace(tzinfo=None)


def init_db():
    """Idempotent database initialization and seeding."""
    # ── Create tables ─────────────────────────────────────────────────────────
    try:
        db.create_all()
        # Smoke-test schema integrity
        User.query.first()
        Command.query.first()
    except Exception as e:
        print(f"[DB] Schema issue detected ({e}). Rebuilding tables …")
        db.session.rollback()
        db.drop_all()
        db.create_all()

    now = _now()

    # ── Seed default admin ────────────────────────────────────────────────────
    if not User.query.filter_by(email='admin@battery.twin').first():
        admin = User(name='System Administrator',
                     email='admin@battery.twin',
                     role='admin')
        admin.set_password('AdminPassword123')
        db.session.add(admin)
        db.session.add(SystemLog(
            event='SYSTEM_INIT',
            description='Default administrator account seeded.',
            timestamp=now,
        ))

    # ── Seed default command row (singleton) ──────────────────────────────────
    if not Command.query.first():
        db.session.add(Command(
            relay=True, fan=False, buzzer=False,
            reset_alarm=False, fan_mode='auto', relay_mode='auto',
            timestamp=now,
        ))
        db.session.add(SystemLog(
            event='SYSTEM_INIT',
            description='Default actuator commands initialised: relay=ON, fan=OFF, buzzer=OFF.',
            timestamp=now,
        ))

    # ── Seed initial telemetry ────────────────────────────────────────────────
    if not SensorData.query.first():
        init_sensor = SensorData(
            temperature=26.5, humidity=52.0, voltage=3.85,
            current=1.20, power=round(3.85 * 1.20, 3),
            battery_percentage=70.8, timestamp=now,
        )
        db.session.add(init_sensor)

        db.session.add(BatteryHealth(
            health_score=98.5, degradation=1.5,
            estimated_cycles=12, estimated_remaining_life=950,
            efficiency=94.5, timestamp=now,
        ))

    # ── Commit all seeds ──────────────────────────────────────────────────────
    try:
        db.session.commit()
        print('[DB] Initialization complete.')
    except Exception as e:
        db.session.rollback()
        print(f'[DB] Seeding error: {e}')
