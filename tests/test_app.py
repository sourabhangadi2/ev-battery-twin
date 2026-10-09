import unittest
import json
from app import create_app, db
from app.models import User, SensorData, BatteryHealth, Alert, Command, SystemLog
from app.twin_engine import V_MIN, V_MAX, get_efficiency
from app.database import init_db


class TestConfig:
    TESTING                       = True
    WTF_CSRF_ENABLED              = False
    SQLALCHEMY_DATABASE_URI       = 'sqlite:///:memory:'
    SECRET_KEY                    = 'test_secret_key_battery_twin_456'
    SQLALCHEMY_TRACK_MODIFICATIONS= False


class EVBatteryTwinTestCase(unittest.TestCase):

    def setUp(self):
        self.app = create_app(TestConfig)
        self.client = self.app.test_client()
        self.ctx = self.app.app_context()
        self.ctx.push()
        db.create_all()

        # Seed minimal state — same as production init_db
        cmd = Command(relay=True, fan=False, buzzer=False,
                      reset_alarm=False, fan_mode='auto', relay_mode='auto')
        db.session.add(cmd)

        self.test_email    = 'operator@test.com'
        self.test_password = 'Password123'
        user = User(name='Test Operator', email=self.test_email, role='operator')
        user.set_password(self.test_password)
        db.session.add(user)
        db.session.commit()

    def tearDown(self):
        db.session.remove()
        db.drop_all()
        self.ctx.pop()

    # ── Helpers ───────────────────────────────────────────────────────────────
    def _post_sensor(self, payload):
        return self.client.post('/update_sensor_data',
                                data=json.dumps(payload),
                                content_type='application/json')

    # ── Auth tests ────────────────────────────────────────────────────────────
    def test_login_flow(self):
        """Operators can log in with correct credentials."""
        r = self.client.post('/login',
                             data={'email': self.test_email, 'password': self.test_password},
                             follow_redirects=True)
        self.assertEqual(r.status_code, 200)
        self.assertIn(b'EV Battery Digital Twin', r.data)

    def test_registration_flow(self):
        """New operator registration succeeds and is stored in DB."""
        new_email = 'newoperator@test.com'
        r = self.client.post('/register', data={
            'name': 'New Operator', 'email': new_email,
            'password': 'SecurePassword123', 'confirm_password': 'SecurePassword123',
        }, follow_redirects=True)
        self.assertEqual(r.status_code, 200)
        self.assertIn(b'Account created successfully', r.data)
        user = User.query.filter_by(email=new_email).first()
        self.assertIsNotNone(user)
        self.assertTrue(user.check_password('SecurePassword123'))

    def test_registration_duplicate_email(self):
        """Registering with an existing email is rejected."""
        r = self.client.post('/register', data={
            'name': 'Dup', 'email': self.test_email,
            'password': 'SecurePassword123', 'confirm_password': 'SecurePassword123',
        }, follow_redirects=True)
        self.assertEqual(r.status_code, 200)
        self.assertIn(b'already registered', r.data)

    def test_registration_password_mismatch(self):
        """Password mismatch fails WTForms validation."""
        r = self.client.post('/register', data={
            'name': 'Mismatch', 'email': 'mismatch@test.com',
            'password': 'SecurePassword123', 'confirm_password': 'Different123',
        }, follow_redirects=True)
        self.assertEqual(r.status_code, 200)
        self.assertIn(b'do not match', r.data)

    def test_unauthorized_dashboard_access(self):
        """Unauthenticated access to dashboard redirects to login."""
        r = self.client.get('/dashboard', follow_redirects=False)
        self.assertEqual(r.status_code, 302)
        self.assertIn('/login', r.headers['Location'])

    # ── Sensor ingest & digital twin ──────────────────────────────────────────
    def test_nominal_sensor_update(self):
        """Nominal sensor values produce healthy state: relay closed, no alarms."""
        r = self._post_sensor({'temperature': 25.0, 'humidity': 50.0, 'voltage': 3.70, 'current': 1.20})
        self.assertEqual(r.status_code, 200)
        data = json.loads(r.data)
        self.assertTrue(data['commands']['relay'])
        self.assertFalse(data['commands']['fan'])
        self.assertFalse(data['commands']['buzzer'])

        sensor = SensorData.query.order_by(SensorData.id.desc()).first()
        self.assertIsNotNone(sensor)
        self.assertAlmostEqual(sensor.temperature, 25.0)
        self.assertAlmostEqual(sensor.power, round(3.70 * 1.20, 3), places=2)

        health = BatteryHealth.query.order_by(BatteryHealth.id.desc()).first()
        self.assertIsNotNone(health)
        self.assertGreater(health.health_score, 90.0)
        self.assertGreater(health.efficiency, 0.0)

    def test_power_computation(self):
        """Power stored as V * |I| rounded to 3 decimal places."""
        r = self._post_sensor({'temperature': 22.0, 'humidity': 48.0, 'voltage': 4.00, 'current': 2.50})
        self.assertEqual(r.status_code, 200)
        sensor = SensorData.query.order_by(SensorData.id.desc()).first()
        self.assertAlmostEqual(sensor.power, round(4.00 * 2.50, 3), places=2)

    def test_efficiency_calculation(self):
        """get_efficiency() returns 100% at zero current and <100% under load."""
        self.assertAlmostEqual(get_efficiency(3.7, 0.0, 25.0), 100.0)
        eff = get_efficiency(3.7, 2.0, 25.0)
        self.assertGreater(eff, 0.0)
        self.assertLess(eff, 100.0)

    # ── Healing engine ────────────────────────────────────────────────────────
    def test_overtemp_healing_warning(self):
        """Temperature > 45°C triggers cooling fan but does not isolate load."""
        r = self._post_sensor({'temperature': 48.0, 'humidity': 45.0, 'voltage': 3.80, 'current': 1.0})
        self.assertEqual(r.status_code, 200)
        data = json.loads(r.data)
        self.assertTrue(data['commands']['relay'])
        self.assertTrue(data['commands']['fan'])
        alert = Alert.query.filter_by(alert_type='High-Temperature', resolved=False).first()
        self.assertIsNotNone(alert)
        self.assertEqual(alert.severity, 'WARNING')

    def test_overtemp_healing_critical(self):
        """Temperature > 55°C isolates battery and sounds alarm."""
        r = self._post_sensor({'temperature': 58.0, 'humidity': 45.0, 'voltage': 3.80, 'current': 1.0})
        self.assertEqual(r.status_code, 200)
        data = json.loads(r.data)
        self.assertFalse(data['commands']['relay'])
        self.assertTrue(data['commands']['fan'])
        self.assertTrue(data['commands']['buzzer'])
        alert = Alert.query.filter_by(alert_type='Over-Temperature', resolved=False).first()
        self.assertIsNotNone(alert)
        self.assertEqual(alert.severity, 'CRITICAL')

    def test_overcurrent_fault(self):
        """Current > 5.0 A forces instant load isolation and alarm."""
        r = self._post_sensor({'temperature': 25.0, 'humidity': 50.0, 'voltage': 3.70, 'current': 5.50})
        self.assertEqual(r.status_code, 200)
        data = json.loads(r.data)
        self.assertFalse(data['commands']['relay'])
        self.assertTrue(data['commands']['buzzer'])
        alert = Alert.query.filter_by(alert_type='Over-Current', resolved=False).first()
        self.assertIsNotNone(alert)
        self.assertEqual(alert.severity, 'CRITICAL')

    def test_overvoltage_fault(self):
        """Voltage > 4.25 V triggers overcharge protection."""
        r = self._post_sensor({'temperature': 25.0, 'humidity': 50.0, 'voltage': 4.30, 'current': 1.00})
        self.assertEqual(r.status_code, 200)
        data = json.loads(r.data)
        self.assertFalse(data['commands']['relay'])
        self.assertTrue(data['commands']['buzzer'])
        alert = Alert.query.filter_by(alert_type='Over-Voltage', resolved=False).first()
        self.assertIsNotNone(alert)
        self.assertEqual(alert.severity, 'CRITICAL')

    def test_invalid_sensor_data_rejected(self):
        """Out-of-range temperature is rejected with 400."""
        r = self._post_sensor({'temperature': 999.0, 'humidity': 50.0, 'voltage': 3.70, 'current': 1.0})
        self.assertEqual(r.status_code, 400)

    def test_missing_sensor_fields(self):
        """Missing required sensor field returns 400."""
        r = self._post_sensor({'temperature': 25.0})
        self.assertEqual(r.status_code, 400)


if __name__ == '__main__':
    unittest.main()
