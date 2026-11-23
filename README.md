# FieldSense — Smart Agriculture IoT Cloud System

A complete end-to-end IoT and Cloud Smart Agriculture system designed for field monitoring, automated irrigation, live video streaming, and AI plant disease diagnosis.

Built for **ESP32 DevKit V1**, **ESP32-CAM**, **FastAPI Backend**, and fully ready for one-click deployment on **Render.com**.

---

## 🌟 System Architecture

```
                                  +------------------------------------+
                                  |     Render.com Cloud Service       |
                                  |  - FastAPI Backend (main.py)       |
                                  |  - Web Dashboard (web/index.html)  |
                                  |  - Open-Meteo Weather Integration  |
                                  |  - Plant Disease AI Engine         |
                                  +------------------------------------+
                                            ▲                ▲
                       HTTPS Telemetry POST |                | Polling Commands (GET)
                                            ▼                ▼
+-----------------------------------------------------------------------------------------+
|                                      FARM FIELD NODE                                    |
|                                                                                         |
|   +------------------------------------+               +----------------------------+   |
|   |         ESP32 DevKit V1            |               |    ESP32-CAM AI Thinker    |   |
|   |  - DHT11 (Temp & Humidity: GPIO 4) |               |  - Live MJPEG Stream       |   |
|   |  - Capacitive Soil (GPIO 34)       |               |  - OV2640 Lens (:81/stream)|   |
|   |  - Rain Sensor (GPIO 35)           |               +----------------------------+   |
|   |  - Flame Sensor (GPIO 27)          |                             |                  |
|   |  - 12V Pump MOSFET (GPIO 26)       |                             | Local Wi-Fi      |
|   +------------------------------------+                             ▼                  |
|                     |                                    FieldSense Dashboard           |
|                     ▼                                    (Live Camera Feed)             |
|             12V DC Water Pump                                                           |
+-----------------------------------------------------------------------------------------+
```

---

## 📁 Repository Structure

```text
FieldSense/
│
├── main.py                          # FastAPI cloud backend & static web server
├── requirements.txt                 # Python dependencies for Render build
├── runtime.txt                      # Python runtime version (3.11.9)
├── .gitignore                       # Git ignore rules
│
├── web/
│   └── index.html                   # Responsive dashboard with live API integration
│
├── firmware/
│   ├── FieldSense_ESP32/
│   │   └── FieldSense_ESP32.ino     # Complete ESP32 DevKit V1 Arduino firmware
│   └── ESP32_CAM/
│       └── CameraWebServer_Guide.md # Flashing and streaming setup for ESP32-CAM
│
├── circuits/
│   └── WIRING_AND_CIRCUITS.md       # Pinout tables, MOSFET driver schematic, power plan
│
└── README.md                        # Master step-by-step setup guide
```

---

## 🚀 Step 1: Deploying to Render.com (Cloud Website)

You can host your website and API on Render for free.

### 1. Push Code to GitHub
1. Create a new repository on [GitHub](https://github.com/) named `FieldSense`.
2. Initialize and push your project:
   ```bash
   git init
   git add .
   git commit -m "Initial commit: FieldSense Smart Agriculture Cloud System"
   git branch -M main
   git remote add origin https://github.com/YOUR_USERNAME/FieldSense.git
   git push -u origin main
   ```

### 2. Create Web Service on Render
1. Log in to [Render](https://render.com/).
2. Click **New +** → **Web Service**.
3. Select your `FieldSense` repository.
4. Configure the service settings:
   - **Name**: `fieldsense` (or your choice)
   - **Language**: `Python 3`
   - **Branch**: `main`
   - **Region**: Closest to you (e.g., Singapore / Frankfurt / Oregon)
   - **Build Command**: `pip install -r requirements.txt`
   - **Start Command**: `uvicorn main:app --host 0.0.0.0 --port $PORT`
   - **Instance Type**: `Free`

### 3. Add Environment Variables in Render
Go to the **Environment** tab of your Render service and add:

| Key | Value | Description |
| :--- | :--- | :--- |
| `DEVICE_TOKEN` | `FieldSenseSecretToken2026` | Token used by ESP32 to authenticate |
| `FARMER_USERNAME` | `farmer` | Dashboard login username |
| `FARMER_PASSWORD` | `farm123` | Dashboard login password |
| `PYTHON_VERSION` | `3.11.9` | Matches runtime.txt |

Click **Save Changes** and wait 2-3 minutes for the build to finish.
Render will provide your live URL: `https://your-service-name.onrender.com`.

---

## ⚡ Step 2: Hardware Assembly & Connections

Refer to [`circuits/WIRING_AND_CIRCUITS.md`](file:///circuits/WIRING_AND_CIRCUITS.md) for full circuit diagrams.

### Pin Summary for ESP32 DevKit V1:
- **DHT11 Data** ➔ **GPIO 4**
- **Capacitive Soil Moisture Analog Out** ➔ **GPIO 34** (ADC1)
- **Rain Sensor Analog Out** ➔ **GPIO 35** (ADC1)
- **Flame Sensor Digital Out (DO)** ➔ **GPIO 27**
- **12V Pump Gate / Relay IN** ➔ **GPIO 26**
- **Sensor VCC** ➔ **3.3V Pin** on ESP32
- **Common Ground** ➔ Connect **ESP32 GND** and **12V Power Supply GND** together.

---

## 💻 Step 3: Flashing the ESP32 Sensor Controller

1. Install [Arduino IDE](https://www.arduino.cc/en/software).
2. Add ESP32 board support via **File → Preferences → Additional Boards Manager URLs**:
   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. In **Tools → Manage Libraries...**, install:
   - **DHT sensor library** (by Adafruit)
   - **Adafruit Unified Sensor** (by Adafruit)
   - **ArduinoJson** (by Benoit Blanchon)
4. Open [`firmware/FieldSense_ESP32/FieldSense_ESP32.ino`](file:///firmware/FieldSense_ESP32/FieldSense_ESP32.ino).
5. Update your Wi-Fi credentials and Render URL:
   ```cpp
   const char* WIFI_SSID     = "YOUR_WIFI_NAME";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   const char* SERVER_URL    = "https://your-service-name.onrender.com";
   const char* DEVICE_TOKEN  = "FieldSenseSecretToken2026";
   ```
6. Select Board: **ESP32 Dev Module**, select your COM port, and click **Upload**.
7. Open Serial Monitor at **115200 baud** to verify connection.

---

## 📷 Step 4: Setting up ESP32-CAM

Follow the detailed instructions in [`firmware/ESP32_CAM/CameraWebServer_Guide.md`](file:///firmware/ESP32_CAM/CameraWebServer_Guide.md):
1. Connect ESP32-CAM via USB-to-TTL programmer with **GPIO 0 connected to GND**.
2. Flash **CameraWebServer** example with `#define CAMERA_MODEL_AI_THINKER` selected.
3. Disconnect GPIO 0, reboot, and copy the IP address printed in Serial Monitor (e.g., `http://192.168.1.85:81/stream`).
4. Paste the URL into the **Live Camera** box on your FieldSense website and click **Connect**.

---

## 🧪 Step 5: Testing Checklist

- [ ] **Render Cloud Health**: Visit `https://your-service.onrender.com/api/health` and verify `{"status": "online"}`.
- [ ] **Login**: Open website URL, log in with `farmer` / `farm123`.
- [ ] **Live Telemetry**: Watch temperature, humidity, and soil moisture update every 5 seconds.
- [ ] **Soil Moisture Test**: Touch capacitive sensor or place in moist soil; watch the percentage change.
- [ ] **Rain Sensor Test**: Drop a droplet of water on the rain sensor plate; verify dashboard displays `🌧️ Rain Detected`.
- [ ] **Flame Interlock Test**: Test flame sensor with light/lighter safely; confirm dashboard triggers **CRITICAL FIRE ALERT** and forces pump OFF immediately.
- [ ] **Water Pump Toggle**: Click pump button on dashboard; observe relay click or MOSFET turn ON.
- [ ] **AI Plant Disease Scan**: Upload an image of a leaf; view immediate AI classification with treatment recommendations.
- [ ] **10-Day Weather**: Type your district or city name; verify 10-day forecast table updates.
