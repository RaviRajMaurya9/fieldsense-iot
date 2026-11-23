# ESP32-CAM AI Thinker Setup Guide for FieldSense

The **ESP32-CAM (AI Thinker module with OV2640 camera)** acts as a dedicated live streaming node for your agricultural field. It runs independently of the main sensor ESP32 and streams real-time MJPEG video across your local network and the FieldSense dashboard.

---

## 1. Hardware Pinout & Flashing Connections

The ESP32-CAM lacks a built-in USB port, so it is programmed using a standard **USB-to-TTL UART programmer** (e.g., FTDI FT232RL or CP2102).

### Wiring for Flashing (Programming Mode)

| USB-to-TTL Adapter | ESP32-CAM Pin | Purpose |
| :--- | :--- | :--- |
| **5V** | **5V** | Power supply (Provide at least 1.5A to 2A) |
| **GND** | **GND** | Common ground |
| **TX** | **U0R (GPIO 3)** | UART Receive |
| **RX** | **U0T (GPIO 1)** | UART Transmit |
| **GND** | **GPIO 0** | **Connect to GND during flashing only!** |

> [!IMPORTANT]
> - Always connect **GPIO 0 to GND** before powering on or pressing the RST button to enter flashing mode.
> - After flashing finishes, **disconnect GPIO 0 from GND** and press the RST button to run the camera code.
> - The OV2640 camera draws brief spikes of current up to 350-450mA when transmitting video. Ensure your 5V power source is steady; otherwise the board will get stuck in a boot loop (`Brownout detector was triggered`).

---

## 2. Arduino IDE Flashing Steps

1. Open **Arduino IDE**.
2. Go to **File → Examples → ESP32 → Camera → CameraWebServer**.
3. In the sketch tabs, open the main `.ino` file and find the camera model definitions:
   ```cpp
   // Select camera model
   //#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
   //#define CAMERA_MODEL_ESP_EYE // Has PSRAM
   //#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
   //#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
   //#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
   //#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
   //#define CAMERA_MODEL_M5STACK_ESP32S3_UNIT_CAM // Has PSRAM
   #define CAMERA_MODEL_AI_THINKER // Has PSRAM
   ```
   **Uncomment `#define CAMERA_MODEL_AI_THINKER`** and ensure all other camera models are commented out.
4. Enter your Wi-Fi credentials:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```
5. In Arduino IDE menu, configure your board settings:
   - **Board**: `AI Thinker ESP32-CAM` (or `ESP32 Wrover Module`)
   - **Upload Speed**: `115200`
   - **Flash Frequency**: `40MHz`
   - **Flash Mode**: `QIO`
   - **Partition Scheme**: `Huge APP (3MB No OTA/1MB SPIFFS)`
   - **PSRAM**: `Enabled`
6. Put the board into download mode (GPIO 0 connected to GND), then click **Upload**.
7. Once upload says `Done uploading`, remove the jumper connecting GPIO 0 to GND, open the **Serial Monitor at 115200 baud**, and press the on-board **RST** button.

---

## 3. Retrieving the Stream URL

In the Serial Monitor, you will see output like:
```text
WiFi connected
Starting web server on port: '80'
Starting stream server on port: '81'
Camera Ready! Use 'http://192.168.1.85' to connect
```

- **Camera Control Panel**: `http://192.168.1.85/`
- **Direct Video Stream URL**: `http://192.168.1.85:81/stream`

---

## 4. Connecting to FieldSense Dashboard

1. In your FieldSense website dashboard, scroll to the **Live Field Camera** section.
2. In the input box, paste:
   ```text
   http://192.168.1.85:81/stream
   ```
   *(Replace with your actual camera IP address)*
3. Click **Connect**. Your real-time field camera video stream will appear directly on your dashboard!

---

## 5. Streaming Remotely (Over the Internet)

If your website is hosted on Render (HTTPS), your browser will prevent loading an unencrypted `http://192.168.x.x` stream due to mixed content security policies when accessed outside your local network.

To view your camera feed from anywhere:
1. **Option A (Local Testing - Recommended for prototype)**:
   Access the dashboard and camera from your computer or phone while connected to the same home/farm Wi-Fi network.
2. **Option B (Ngrok Secure Tunneling)**:
   Run ngrok on a local computer on the same network:
   ```bash
   ngrok http http://192.168.137.73:81
   ```
   Copy the `https://xxxx.ngrok-free.app/stream` URL and paste it into FieldSense.

---

## 6. How to Maximize Quality & Eliminate Streaming Lag

If your live stream stutters, freezes, or falls seconds behind real-time, apply these optimizations:

### A. Arduino Code Optimization (`CameraWebServer.ino`):
Inside the `setup()` function of the `CameraWebServer` example, verify these `camera_config_t` parameters:

```cpp
config.frame_size = FRAMESIZE_VGA;  // 640x480 (Optimal: 25-30 FPS without Wi-Fi buffer lag)
config.jpeg_quality = 12;           // 10-14 (Crisp sharp picture without huge packet sizes)
config.fb_count = 2;                // Double buffering in PSRAM (Prevents frame tearing)
config.xclk_freq_hz = 20000000;     // 20 MHz sensor clock (Doubles frame readout speed)
config.grab_mode = CAMERA_GRAB_LATEST; // Drops stale frames and ALWAYS serves the latest real-time frame!
```

> [!TIP]
> Setting `config.grab_mode = CAMERA_GRAB_LATEST;` (available in ESP32 board package 2.0.4+) instructs the ESP32 to immediately discard older queued frames if the Wi-Fi client is slightly slow, completely eliminating stream delay!

### B. Hardware & Power Stability:
1. **Add a 470µF or 1000µF Capacitor** directly across the `5V` and `GND` pins on the ESP32-CAM board. The camera Wi-Fi module pulls sudden 450mA peaks; voltage drops cause packet re-transmissions and video stutter.
2. Ensure your power supply delivers a true **5V @ 2A**. Powering the camera from a 3.3V pin or a cheap USB-TTL converter causes brownout lag.

### C. Live Dashboard Controls:
- **⚡ Zero-Lag Sync**: Click this button in the web dashboard if the stream ever buffers; it flushes the browser's TCP socket buffer and snaps to real-time.
- **Smooth 30 FPS / HD Quality**: Switches resolution and quality dynamically over HTTP.

   Copy the `https://xxxx.ngrok-free.app/stream` URL and paste it into FieldSense.
3. **Option C (ESP32-CAM Snapshot Capture to Render API)**:
   The ESP32-CAM can periodically capture a JPEG snapshot and POST it to `/api/camera/snapshot` on your Render backend.
