"""
FieldSense — USB Serial COM Port Bridge
Reads live sensor telemetry directly from an ESP32 connected via USB cable
and forwards it to http://127.0.0.1:8000/api/device/telemetry.
"""

import time
import json
import re
import httpx

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial is not installed. Run: pip install pyserial")
    exit(1)

SERVER_URL = "http://127.0.0.1:8000"
DEVICE_TOKEN = "FieldSenseSecretToken2026"
BAUD_RATE = 115200

def find_esp32_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None
    for p in ports:
        desc = (p.description or "").lower()
        # Common USB-to-UART chips: CP210x, CH340, FTDI, USB Serial
        if any(chip in desc for chip in ["cp210", "ch340", "ftdi", "uart", "usb"]):
            return p.device
    return ports[0].device

port_name = find_esp32_port()

print("==================================================")
print("  FieldSense — USB Serial Hardware Bridge         ")
print("==================================================")

if not port_name:
    print("No USB COM ports detected. Please plug your ESP32 into a USB port!")
    print("Available ports list was empty.")
    exit(0)

print(f"Found USB Serial Port: {port_name}")
print(f"Connecting at {BAUD_RATE} baud...")

try:
    ser = serial.Serial(port_name, BAUD_RATE, timeout=2)
    time.sleep(2)  # Wait for ESP32 reset after serial connect
    print(f"Connected to ESP32 on {port_name}!\nForwarding data to {SERVER_URL}...\n")
except Exception as e:
    print(f"Error opening port {port_name}: {e}")
    exit(1)

# Regex to parse Serial.printf formatted strings:
# "Temp: 28.5 C | Hum: 65.0 % | Soil: 48 % (raw: 2200) | Rain: NO (raw: 2800) | Flame: Safe"
pattern = re.compile(
    r"Temp:\s*([\d\.]+).*?Hum:\s*([\d\.]+).*?Soil:\s*([\d\.]+).*?raw:\s*(\d+).*?Rain:\s*(YES|NO).*?raw:\s*(\d+).*?Flame:\s*(\w+)",
    re.IGNORECASE
)

client = httpx.Client(timeout=4)

try:
    while True:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if not line:
            continue

        print(f"[ESP32 SERIAL] {line}")

        # Check if line matches sensor format
        match = pattern.search(line)
        if match:
            temp = float(match.group(1))
            hum = float(match.group(2))
            soil_pct = float(match.group(3))
            soil_raw = int(match.group(4))
            raining = (match.group(5).upper() == "YES")
            rain_raw = int(match.group(6))
            flame = ("ALERT" in match.group(7).upper())

            payload = {
                "temperature_c": temp,
                "humidity_pct": hum,
                "soil_raw": soil_raw,
                "soil_moisture_pct": soil_pct,
                "rain_raw": rain_raw,
                "raining": raining,
                "fire_detected": flame,
                "pump_on": False
            }

            try:
                res = client.post(
                    f"{SERVER_URL}/api/device/telemetry",
                    headers={"Content-Type": "application/json", "X-Device-Token": DEVICE_TOKEN},
                    json=payload
                )
                print(f"   -> Forwarded to Webpage (HTTP {res.status_code})")
            except Exception as err:
                print(f"   -> Web server error: {err}")

except KeyboardInterrupt:
    print("\nBridge stopped by user.")
    ser.close()
