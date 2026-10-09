import urllib.request
import urllib.parse
import json
import http.cookiejar
import re

def verify_live():
    print("=== LIVE INTEGRATION & DASHBOARD VERIFICATION ===")
    cj = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
    
    # 1. Fetch Login Page
    login_html = opener.open("http://127.0.0.1:5000/login").read().decode("utf-8")
    token_match = re.search(r'name="csrf_token"[^>]*value="([^"]+)"', login_html)
    csrf_token = token_match.group(1) if token_match else ""
    print(f"[+] Login page accessed. CSRF Token obtained: {bool(csrf_token)}")
    
    # 2. Authenticate as Admin
    login_payload = urllib.parse.urlencode({
        "email": "admin@battery.twin",
        "password": "AdminPassword123",
        "csrf_token": csrf_token
    }).encode("utf-8")
    login_res = opener.open("http://127.0.0.1:5000/login", data=login_payload)
    print(f"[+] Admin authentication successful (HTTP {login_res.status})")
    
    # 3. Fetch Dashboard HTML
    dash_html = opener.open("http://127.0.0.1:5000/dashboard").read().decode("utf-8")
    assert "EV Battery Digital Twin" in dash_html
    print("[+] Dashboard rendered successfully")
    
    # 4. Fetch live sensor data API
    sensor_res = opener.open("http://127.0.0.1:5000/get_sensor_data")
    data = json.loads(sensor_res.read().decode("utf-8"))
    print("[+] Telemetry Data:", json.dumps(data["telemetry"], indent=2))
    print("[+] Battery Health (Digital Twin):", json.dumps(data["health"], indent=2))
    print("[+] Commands:", json.dumps(data["commands"], indent=2))
    print(f"[+] Active Alerts Count: {len(data['alerts'])}")
    print(f"[+] System Logs Count: {len(data['logs'])}")
    print(f"[+] Chart History Timestamps: {len(data['chart_data']['timestamps'])}")
    
    # 5. Send manual command override (Relay auto)
    cmd_payload = json.dumps({"device": "reset_alarm", "value": True}).encode("utf-8")
    req = urllib.request.Request(
        "http://127.0.0.1:5000/send_command",
        data=cmd_payload,
        headers={"Content-Type": "application/json"}
    )
    cmd_res = opener.open(req)
    print(f"[+] Manual Reset Alarm Override sent: {cmd_res.read().decode('utf-8').strip()}")
    
    # 6. Send recovery telemetry
    recovery_payload = json.dumps({
        "temperature": 26.5,
        "humidity": 48.0,
        "voltage": 3.82,
        "current": 1.10
    }).encode("utf-8")
    rec_req = urllib.request.Request(
        "http://127.0.0.1:5000/update_sensor_data",
        data=recovery_payload,
        headers={"Content-Type": "application/json"}
    )
    rec_res = opener.open(rec_req)
    rec_data = json.loads(rec_res.read().decode("utf-8"))
    print("[+] Post-Recovery Telemetry Commands:", json.dumps(rec_data["commands"], indent=2))
    assert rec_data["commands"]["relay"] is True, "Relay should be connected post-recovery"
    print("=== ALL SYSTEM VALIDATION TESTS PASSED PERFECTLY ===")

if __name__ == "__main__":
    verify_live()
