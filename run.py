import os
import socket
from app import create_app, db
from app.database import init_db

def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return '127.0.0.1'

app = create_app()

if __name__ == '__main__':
    # Ensure database directory exists
    db_dir = os.path.join(os.path.abspath(os.path.dirname(__file__)), 'database')
    os.makedirs(db_dir, exist_ok=True)

    with app.app_context():
        init_db()

    local_ip = get_local_ip()
    mobile_url = f"http://{local_ip}:5000"

    print('=' * 65)
    print(' Self-Healing EV Battery Simulator — Digital Twin Server')
    print(' Local PC Access : http://127.0.0.1:5000')
    print(f' Mobile Access   : {mobile_url}  (Connect to same Wi-Fi)')
    print(' Admin Login     : admin@battery.twin / AdminPassword123')
    print('=' * 65)

    app.run(host='0.0.0.0', port=5000, debug=True)
