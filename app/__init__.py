import os
from flask import Flask
from flask_login import LoginManager
from flask_wtf.csrf import CSRFProtect
from app.models import db, User

login_manager = LoginManager()
csrf         = CSRFProtect()

def create_app(config_class='config.Config'):
    app = Flask(__name__)

    # ── Load configuration ────────────────────────────────────────────────────
    if isinstance(config_class, str):
        app.config.from_object(config_class)
    else:
        # Allow dict / object (used by tests)
        app.config.from_object(config_class)

    # ── Ensure database directory exists ─────────────────────────────────────
    db_uri = app.config.get('SQLALCHEMY_DATABASE_URI', '')
    if db_uri.startswith('sqlite:///'):
        db_path = db_uri.replace('sqlite:///', '')
        os.makedirs(os.path.dirname(os.path.abspath(db_path)), exist_ok=True)

    # ── Bind extensions ───────────────────────────────────────────────────────
    db.init_app(app)
    login_manager.init_app(app)
    csrf.init_app(app)

    # ── Session & login config ─────────────────────────────────────────────────
    login_manager.login_view          = 'main.login'
    login_manager.login_message       = 'Please log in to access the operator dashboard.'
    login_manager.login_message_category = 'warning'

    @login_manager.user_loader
    def load_user(user_id):
        return db.session.get(User, int(user_id))

    # ── Register blueprint ────────────────────────────────────────────────────
    from app.routes import bp as main_bp
    app.register_blueprint(main_bp)

    return app
