import os

class Config:
    """Production-ready application configuration."""
    
    # ── Security ─────────────────────────────────────────────────────────────
    SECRET_KEY = os.environ.get('SECRET_KEY') or 'dev_key_battery_twin_self_healing_987654'
    
    # ── Database ──────────────────────────────────────────────────────────────
    BASE_DIR = os.path.abspath(os.path.dirname(__file__))
    DB_PATH  = os.path.join(BASE_DIR, 'database', 'battery_twin.db')
    SQLALCHEMY_DATABASE_URI = os.environ.get('DATABASE_URL') or f'sqlite:///{DB_PATH}'
    SQLALCHEMY_TRACK_MODIFICATIONS = False
    SQLALCHEMY_ENGINE_OPTIONS = {
        'pool_pre_ping': True,
        'connect_args': {'check_same_thread': False, 'timeout': 20}
    }
    
    # ── Session ───────────────────────────────────────────────────────────────
    SESSION_COOKIE_SECURE   = False   # Set True in production with HTTPS
    SESSION_COOKIE_HTTPONLY = True
    SESSION_COOKIE_SAMESITE = 'Lax'
    PERMANENT_SESSION_LIFETIME = 86400  # 24 hours
