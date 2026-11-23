"""
FieldSense — Real-Time Hardware Simulator for Localhost
Simulates an actual ESP32 node sending live fluctuating sensor readings
to http://127.0.0.1:8000/api/device/telemetry.
"""

import time
import math
import random
import httpx

SERVER_URL = "http://127.0.0.1:8000"
DEVICE_TOKEN = "FieldSenseSecretToken2026"

print("==================================================")
print("  FieldSense — Live Hardware Sensor Simulator     ")
print(f"  Target: {SERVER_URL}/api/device/telemetry       ")
print("==================================================")
print("Sending live fluctuating sensor stream every 2.5 seconds...")
print("Open your browser at http://localhost:8000 to watch live values change!\n")

step = 0
base_temp = 28.5
base_hum = 64.0
soil_pct = 54.0
pump_on = False

try:
    with httpx.Client(timeout=4) as client:
        while True:
            step += 1

            # 1. Check if user clicked pump ON/OFF from the webpage
            try:
                cmd_res = client.get(
                    f"{SERVER_URL}/api/device/next-command",
                    headers={"X-Device-Token": DEVICE_TOKEN}
                )
                if cmd_res.status_code == 200:
                    desired_pump = cmd_res.json().get("pump_on", False)
                    if desired_pump != pump_on:
                        pump_on = desired_pump
                        print(f"⚡ [PUMP COMMAND DETECTED FROM WEBPAGE] Pump is now: {'ON (Watering)' if pump_on else 'OFF'}")
            except Exception as e:
                print(f"Warning: Could not fetch command: {e}")

            # 2. Simulate realistic sensor fluctuations
            # Temperature rises slightly with sine wave + small noise
            temp = round(base_temp + 2.5 * math.sin(step * 0.15) + random.uniform(-0.3, 0.3), 1)
            humidity = round(base_hum - 3.0 * math.sin(step * 0.15) + random.uniform(-0.5, 0.5), 1)

            # Soil moisture: If pump is ON, moisture increases; if OFF, it slowly evaporates
            if pump_on:
                soil_pct = min(85.0, soil_pct + random.uniform(1.2, 2.5))
            else:
                soil_pct = max(22.0, soil_pct - random.uniform(0.2, 0.6))
            
            # ADC raw mappings
            soil_raw = int(3100 - (soil_pct / 100.0) * (3100 - 1350))

            # Rain simulation: periodic brief shower
            is_raining = (step % 25 in [18, 19, 20])
            rain_raw = 1450 if is_raining else 3200

            # Fire simulation: false
            is_fire = False

            payload = {
                "temperature_c": temp,
                "humidity_pct": humidity,
                "soil_raw": soil_raw,
                "soil_moisture_pct": round(soil_pct, 1),
                "rain_raw": rain_raw,
                "raining": is_raining,
                "fire_detected": is_fire,
                "pump_on": pump_on
            }

            try:
                res = client.post(
                    f"{SERVER_URL}/api/device/telemetry",
                    headers={
                        "Content-Type": "application/json",
                        "X-Device-Token": DEVICE_TOKEN
                    },
                    json=payload
                )
                if res.status_code == 200:
                    rain_str = "🌧️ RAINING!" if is_raining else "☀️ Clear"
                    pump_str = "🚿 ON" if pump_on else "⚪ OFF"
                    print(f"[{time.strftime('%H:%M:%S')}] Streamed -> Temp: {temp}°C | Hum: {humidity}% | Soil: {round(soil_pct)}% | {rain_str} | Pump: {pump_str}")
                else:
                    print(f"Server response code: {res.status_code} - {res.text}")
            except Exception as err:
                print(f"Could not connect to {SERVER_URL}: {err}")

            time.sleep(2.5)

except KeyboardInterrupt:
    print("\nSimulator stopped by user.")
