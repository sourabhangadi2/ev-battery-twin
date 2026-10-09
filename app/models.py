from datetime import datetime, timezone
from flask_login import UserMixin
from werkzeug.security import generate_password_hash, check_password_hash
from flask_sqlalchemy import SQLAlchemy

db = SQLAlchemy()

# ── Helper ────────────────────────────────────────────────────────────────────
def _now():
    """Return naive UTC datetime (SQLite-compatible)."""
    return datetime.now(timezone.utc).replace(tzinfo=None)


# ── Models ────────────────────────────────────────────────────────────────────
class User(db.Model, UserMixin):
    __tablename__ = 'users'

    id            = db.Column(db.Integer,      primary_key=True)
    name          = db.Column(db.String(100),  nullable=False)
    email         = db.Column(db.String(120),  unique=True, index=True, nullable=False)
    password_hash = db.Column(db.String(256),  nullable=False)
    role          = db.Column(db.String(50),   default='operator')
    created_at    = db.Column(db.DateTime,     default=_now)

    def set_password(self, password: str) -> None:
        self.password_hash = generate_password_hash(password)

    def check_password(self, password: str) -> bool:
        return check_password_hash(self.password_hash, password)

    def to_dict(self) -> dict:
        return {
            'id':         self.id,
            'name':       self.name,
            'email':      self.email,
            'role':       self.role,
            'created_at': self.created_at.isoformat() if self.created_at else None,
        }

    def __repr__(self) -> str:
        return f'<User {self.email} [{self.role}]>'


class SensorData(db.Model):
    __tablename__ = 'sensor_data'

    id                 = db.Column(db.Integer, primary_key=True)
    temperature        = db.Column(db.Float,   nullable=False)
    humidity           = db.Column(db.Float,   nullable=False)
    voltage            = db.Column(db.Float,   nullable=False)
    current            = db.Column(db.Float,   nullable=False)
    power              = db.Column(db.Float,   nullable=False)   # Watts = V × I
    battery_percentage = db.Column(db.Float,   nullable=False)   # SOC %
    timestamp          = db.Column(db.DateTime, default=_now, index=True)

    def to_dict(self) -> dict:
        return {
            'id':                 self.id,
            'temperature':        self.temperature,
            'humidity':           self.humidity,
            'voltage':            self.voltage,
            'current':            self.current,
            'power':              self.power,
            'battery_percentage': self.battery_percentage,
            'timestamp':          self.timestamp.isoformat() if self.timestamp else None,
        }

    def __repr__(self) -> str:
        return f'<SensorData id={self.id} V={self.voltage} T={self.temperature}>'


class BatteryHealth(db.Model):
    __tablename__ = 'battery_health'

    id                       = db.Column(db.Integer, primary_key=True)
    health_score             = db.Column(db.Float,   nullable=False)   # SOH %
    degradation              = db.Column(db.Float,   nullable=False)   # Degradation %
    estimated_cycles         = db.Column(db.Integer, nullable=False)
    estimated_remaining_life = db.Column(db.Integer, nullable=False)   # RUL (cycles)
    efficiency               = db.Column(db.Float,   nullable=False, default=100.0)  # Electrical efficiency %
    timestamp                = db.Column(db.DateTime, default=_now, index=True)

    def to_dict(self) -> dict:
        return {
            'id':                       self.id,
            'health_score':             self.health_score,
            'degradation':              self.degradation,
            'estimated_cycles':         self.estimated_cycles,
            'estimated_remaining_life': self.estimated_remaining_life,
            'efficiency':               self.efficiency,
            'timestamp':                self.timestamp.isoformat() if self.timestamp else None,
        }

    def __repr__(self) -> str:
        return f'<BatteryHealth id={self.id} SOH={self.health_score}%>'


class Alert(db.Model):
    __tablename__ = 'alerts'

    id         = db.Column(db.Integer,      primary_key=True)
    alert_type = db.Column(db.String(100),  nullable=False)
    severity   = db.Column(db.String(50),   nullable=False)   # 'WARNING' | 'CRITICAL'
    message    = db.Column(db.String(255),  nullable=False)
    timestamp  = db.Column(db.DateTime,    default=_now, index=True)
    resolved   = db.Column(db.Boolean,     default=False, index=True)

    def to_dict(self) -> dict:
        return {
            'id':         self.id,
            'alert_type': self.alert_type,
            'severity':   self.severity,
            'message':    self.message,
            'timestamp':  self.timestamp.isoformat() if self.timestamp else None,
            'resolved':   self.resolved,
        }

    def __repr__(self) -> str:
        return f'<Alert {self.alert_type} [{self.severity}] resolved={self.resolved}>'


class Command(db.Model):
    __tablename__ = 'commands'

    id           = db.Column(db.Integer,    primary_key=True)
    relay        = db.Column(db.Boolean,    default=True,   nullable=False)  # True = connected
    fan          = db.Column(db.Boolean,    default=False,  nullable=False)
    buzzer       = db.Column(db.Boolean,    default=False,  nullable=False)
    reset_alarm  = db.Column(db.Boolean,    default=False,  nullable=False)
    fan_mode     = db.Column(db.String(20), default='auto', nullable=False)  # auto | on | off
    relay_mode   = db.Column(db.String(20), default='auto', nullable=False)
    timestamp    = db.Column(db.DateTime,   default=_now)

    def to_dict(self) -> dict:
        return {
            'id':          self.id,
            'relay':       self.relay,
            'fan':         self.fan,
            'buzzer':      self.buzzer,
            'reset_alarm': self.reset_alarm,
            'fan_mode':    self.fan_mode,
            'relay_mode':  self.relay_mode,
            'timestamp':   self.timestamp.isoformat() if self.timestamp else None,
        }

    def __repr__(self) -> str:
        return f'<Command relay={self.relay} fan={self.fan} buzzer={self.buzzer}>'


class SystemLog(db.Model):
    __tablename__ = 'system_logs'

    id          = db.Column(db.Integer, primary_key=True)
    event       = db.Column(db.String(100), nullable=False)  # AUTH_SUCCESS | HEALING_ACTION | …
    description = db.Column(db.Text,        nullable=False)
    timestamp   = db.Column(db.DateTime,    default=_now, index=True)

    def to_dict(self) -> dict:
        return {
            'id':          self.id,
            'event':       self.event,
            'description': self.description,
            'timestamp':   self.timestamp.isoformat() if self.timestamp else None,
        }

    def __repr__(self) -> str:
        return f'<SystemLog {self.event} @ {self.timestamp}>'
