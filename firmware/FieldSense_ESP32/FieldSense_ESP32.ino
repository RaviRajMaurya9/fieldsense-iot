/*
  =============================================================================
  FieldSense — Smart Agriculture ESP32 Node Firmware (v1.6)
  Board: ESP32 DevKit V1 (ESP-WROOM-32)

  Key Enhancements in v1.6:
    - CONTINUOUS UNINTERRUPTED CONNECTION:
        * Disabled Wi-Fi modem sleep (WiFi.setSleep(false)) to eliminate packet
          drops, latency spikes, and periodic disconnections.
        * Auto-reconnect with WiFi.disconnect() + WiFi.begin() on link loss.
        * Zero socket leaks: http.setReuse(false) and explicit client.stop()
          prevent lwIP socket descriptor exhaustion.
    - HIGH-ACCURACY SENSORS:
        * 12-sample trimmed-mean oversampling for Soil Moisture and Rain ADC
          filters out Wi-Fi RF noise spikes.
        * Calibrated soil moisture thresholds (Dry: 3400, Wet: 1350).
        * Validated DHT11 reading rate (2-second cycle prevents NaN dropouts).
    - DUAL-MODE TELEMETRY:
        * Steady-state continuous 2-second heartbeat telemetry.
        * Instant event-driven push on physical button press or fire emergency.
    - 100% NON-BLOCKING:
        * Physical manual switch & Auto Mode operate reliably offline or online.

  Pinout Map:
    - DHT11 (Temp & Humidity): GPIO 4
    - Capacitive Soil Moisture Sensor: GPIO 34 (ADC1_CH6)
    - Rain Sensor Analog Output: GPIO 35 (ADC1_CH7)
    - Flame Sensor Digital Output: GPIO 27
    - 12V Pump Relay / MOSFET Driver: GPIO 26
    - Physical Pushbutton / Manual Switch: GPIO 13 (Internal Pull-Up to GND)
    - Blue Wi-Fi LED: GPIO 2 (Also onboard ESP32 LED)
    - Red Wi-Fi LED: GPIO 19
  =============================================================================
*/

#include <ArduinoJson.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

// ================= USER CONFIGURATION =================
// 1. Enter your 2.4GHz Wi-Fi details (ESP32 only supports 2.4GHz)
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// 2. BACKEND SERVER URL:
//    - When ESP32 is connected to your Laptop Mobile Hotspot:
//      Use: "http://192.168.137.1:8000"
//    - When ESP32 and Laptop are both on standard home/office Wi-Fi:
//      Use: "http://10.58.27.10:8000"
//    - For RENDER CLOUD deployment:
//      Use: "https://fieldsense-iot.onrender.com"
const char *SERVER_URL = "http://192.168.137.1:8000";

// 3. Secret Device Authentication Token (matches DEVICE_TOKEN on server)
const char *DEVICE_TOKEN = "FieldSenseSecretToken2026";

// ================= PIN DEFINITIONS =================
#define DHT_PIN 4    // DHT11 Data pin (Digital)
#define SOIL_PIN 34  // Capacitive soil moisture sensor analog output (ADC1_CH6)
#define RAIN_PIN 35  // Rain sensor analog output (ADC1_CH7)
#define FLAME_PIN 27 // Flame sensor digital DO output
#define PUMP_PIN 26  // 12V Pump driver (Relay IN or MOSFET Gate)
#define BUTTON_PIN 13 // Physical Pushbutton / Manual Switch (Internal pull-up to GND)

// Wi-Fi Status LED Indicators
#define LED_BLUE_PIN 2  // Blue LED (Also onboard ESP32 LED) -> ON when Wi-Fi connected
#define LED_RED_PIN 19  // Red LED -> ON/Blinking when Wi-Fi disconnected / searching

// ================= RELAY & ACTUATOR CONFIGURATION =================
// Set to true for standard Active-LOW relays (LOW = ON, HIGH = OFF).
// Set to false for Active-HIGH relays or N-Channel MOSFETs (HIGH = ON, LOW = OFF).
const bool RELAY_ACTIVE_LOW = false;

#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// ================= CALIBRATION SETTINGS =================
const int SOIL_DRY = 3400;      // Calibrated ADC for dry air / dry soil
const int SOIL_WET = 1350;      // Calibrated ADC for fully submerged / soaked soil
const int RAIN_THRESHOLD = 2000; // ADC threshold for rain detection
const bool FLAME_ACTIVE_LOW = true;

// Operating Modes & States
bool autoMode = true;      // true = Auto Mode, false = Manual Mode
bool isPumpActive = false; // Current relay / pump state
bool isFireActive = false;
bool sendImmediateTelemetry = false;

// Physical Button Debounce
int buttonState = HIGH;
int lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 40;

// High-reliability 2-Second continuous streaming interval
const unsigned long TELEMETRY_INTERVAL = 2000;
unsigned long lastTelemetryTime = 0;

// Non-blocking Wi-Fi reconnect timer
unsigned long lastWiFiCheckTime = 0;
const unsigned long WIFI_RETRY_INTERVAL = 10000; // Retry every 10s if link drops

// Non-blocking Red LED blink timer (for offline/connecting state)
unsigned long lastLedBlinkTime = 0;
bool redLedBlinkState = false;

// Cached last valid DHT readings (prevents sensor read dropouts)
float lastValidTemp = 28.5;
float lastValidHum = 65.0;
unsigned long lastDhtReadTime = 0;

// ================= PUMP / RELAY ACTUATOR =================
void setPumpState(bool state) {
  isPumpActive = state;

  if (RELAY_ACTIVE_LOW) {
    digitalWrite(PUMP_PIN, isPumpActive ? LOW : HIGH);
  } else {
    digitalWrite(PUMP_PIN, isPumpActive ? HIGH : LOW);
  }

  Serial.printf("[RELAY] Pump -> %s | Pin %d = %s (ActiveLow=%s)\n",
                isPumpActive ? "ON (Pumping)" : "OFF (Idle)", PUMP_PIN,
                digitalRead(PUMP_PIN) == HIGH ? "HIGH" : "LOW",
                RELAY_ACTIVE_LOW ? "YES" : "NO");
}

// ================= LED INDICATOR HELPER =================
void updateWiFiLEDs() {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_BLUE_PIN, HIGH); // Blue solid ON
    digitalWrite(LED_RED_PIN, LOW);   // Red OFF
  } else {
    digitalWrite(LED_BLUE_PIN, LOW);  // Blue OFF
    // Non-blocking blink for Red LED (every 500ms)
    unsigned long now = millis();
    if (now - lastLedBlinkTime >= 500) {
      lastLedBlinkTime = now;
      redLedBlinkState = !redLedBlinkState;
      digitalWrite(LED_RED_PIN, redLedBlinkState ? HIGH : LOW);
    }
  }
}

// ================= SENSOR READINGS WITH NOISE FILTERING =================
// 12-sample trimmed-mean oversampling removes Wi-Fi RF spikes
int readAnalogSmoothed(int pin, int samples = 12) {
  if (samples < 4) samples = 4;
  int minVal = 4096;
  int maxVal = -1;
  long sum = 0;

  for (int i = 0; i < samples; i++) {
    int val = analogRead(pin);
    if (val < minVal) minVal = val;
    if (val > maxVal) maxVal = val;
    sum += val;
    delayMicroseconds(120);
  }

  // Remove highest and lowest outlier samples to reject RF electrical noise
  sum = sum - minVal - maxVal;
  return (int)(sum / (samples - 2));
}

int calculateSoilPercentage(int rawAnalog) {
  // If sensor is disconnected / floating to near zero, guard against false 100%
  if (rawAnalog < 200) return 0;
  if (rawAnalog >= SOIL_DRY) return 0;
  if (rawAnalog <= SOIL_WET) return 100;

  int pct = map(rawAnalog, SOIL_DRY, SOIL_WET, 0, 100);
  return constrain(pct, 0, 100);
}

bool checkFlameSensor() {
  int val = digitalRead(FLAME_PIN);
  if (FLAME_ACTIVE_LOW) {
    return (val == LOW);
  }
  return (val == HIGH);
}

// ================= HTTP POST TELEMETRY & BIDIRECTIONAL SYNC =================
bool sendTelemetry(float temp, float humidity, int soilRaw, int soilPct,
                   int rainRaw, bool raining, bool fire) {
  if (WiFi.status() != WL_CONNECTED)
    return false;

  HTTPClient http;
  String endpoint = String(SERVER_URL) + "/api/device/telemetry";
  bool isHttps = endpoint.startsWith("https://");

  WiFiClient standardClient;
  WiFiClientSecure secureClient;

  bool beginSuccess = false;
  if (isHttps) {
    secureClient.setInsecure();
    beginSuccess = http.begin(secureClient, endpoint);
  } else {
    beginSuccess = http.begin(standardClient, endpoint);
  }

  if (!beginSuccess) {
    Serial.println("[HTTP] begin() failed!");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  http.setTimeout(3000); // 3s timeout
  http.setReuse(false);  // CRITICAL: Explicitly do NOT reuse sockets to prevent lwIP socket leaks!

  StaticJsonDocument<384> doc;
  doc["temperature_c"] = round(temp * 10.0) / 10.0;
  doc["humidity_pct"] = round(humidity * 10.0) / 10.0;
  doc["soil_raw"] = soilRaw;
  doc["soil_moisture_pct"] = soilPct;
  doc["rain_raw"] = rainRaw;
  doc["raining"] = raining;
  doc["fire_detected"] = fire;
  doc["pump_on"] = isPumpActive;
  doc["auto_mode"] = autoMode;

  String jsonBody;
  serializeJson(doc, jsonBody);

  int httpCode = http.POST(jsonBody);

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    StaticJsonDocument<256> respDoc;
    if (!deserializeJson(respDoc, response)) {
      // Sync cloud command only if mode differs
      if (respDoc.containsKey("auto_mode")) {
        bool cloudAuto = respDoc["auto_mode"];
        if (cloudAuto != autoMode) {
          autoMode = cloudAuto;
          Serial.printf("[SYNC] Mode shifted from cloud -> %s\n",
                        autoMode ? "AUTO" : "MANUAL");
        }
      }
      // If in Manual Mode, apply web dashboard pump switch
      if (!autoMode && respDoc.containsKey("pump_on")) {
        bool targetPumpState = respDoc["pump_on"] | false;
        if (targetPumpState != isPumpActive) {
          Serial.printf("[WEB COMMAND] Manual pump -> %s\n",
                        targetPumpState ? "ON" : "OFF");
          setPumpState(targetPumpState);
        }
      }
    }
  } else {
    Serial.printf("[HTTP] POST failed, code: %d\n", httpCode);
  }

  // CRITICAL CLEANUP: Properly end HTTP session and stop TCP clients to free lwIP sockets immediately
  http.end();
  if (isHttps) {
    secureClient.stop();
  } else {
    standardClient.stop();
  }

  return (httpCode >= 200 && httpCode < 300);
}

// ================= ARDUINO SETUP =================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n==========================================");
  Serial.println("  FieldSense ESP32 Agriculture Node v1.6  ");
  Serial.println("  Continuous Connection + Accurate Filter ");
  Serial.println("==========================================");

  // 1. Configure Wi-Fi Indicator LEDs (GPIO 2 Blue, GPIO 19 Red)
  pinMode(LED_BLUE_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  digitalWrite(LED_BLUE_PIN, LOW);
  digitalWrite(LED_RED_PIN, HIGH); // Red ON until Wi-Fi connects

  // 2. Configure Physical Button Switch (GPIO 13 to GND)
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 3. Configure Pump / Relay Actuator Pin (GPIO 26)
  pinMode(PUMP_PIN, OUTPUT);

  // 4. STEP-BY-STEP RELAY HARDWARE DIAGNOSTIC TEST:
  Serial.println("\n----------------------------------------------");
  Serial.println(">>> RELAY HARDWARE VOLTAGE TEST ON BOOT <<<");
  Serial.println("[TEST 1/2] Driving GPIO 26 HIGH (3.3V) for 1.2s...");
  digitalWrite(PUMP_PIN, HIGH);
  delay(1200);

  Serial.println("[TEST 2/2] Driving GPIO 26 LOW (0.0V) for 1.2s...");
  digitalWrite(PUMP_PIN, LOW);
  delay(1200);

  // Restore relay to idle OFF state based on RELAY_ACTIVE_LOW setting
  setPumpState(false);
  Serial.println(">>> TEST COMPLETE: Relay restored to idle OFF. <<<");
  Serial.println("----------------------------------------------\n");

  // 5. Configure Sensor Inputs
  pinMode(FLAME_PIN, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  dht.begin();
  Serial.println("[INIT] All Sensors & Hardware Switch initialized.");

  // 6. ROCK-SOLID WI-FI CONFIGURATION
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.setSleep(false); // CRITICAL: Disable modem sleep to prevent latency spikes and dropouts!

  Serial.println("[WiFi] Starting connection to: " + String(WIFI_SSID));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Quick 3-second non-blocking attempt on boot
  unsigned long startWait = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startWait < 3000)) {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_BLUE_PIN, HIGH);
    digitalWrite(LED_RED_PIN, LOW);
    Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] Initial link pending. Entering loop (Physical switch active offline!).");
  }
}

// ================= ARDUINO LOOP =================
void loop() {
  unsigned long currentMillis = millis();

  // -------------------------------------------------------------
  // 1. PHYSICAL MANUAL SWITCH (Operates ANY TIME, offline or online!)
  // -------------------------------------------------------------
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonReading) {
    lastDebounceTime = currentMillis;
  }
  if ((currentMillis - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) { // Button pressed (Pin shorted to GND)
        autoMode = false; // Manual button switches to MANUAL mode immediately!
        setPumpState(!isPumpActive); // Toggle Relay ON/OFF
        sendImmediateTelemetry = true; // Push update to cloud immediately!
        Serial.printf(
            "🔘 [PHYSICAL BUTTON PRESSED] Manual Mode -> Pump is now %s\n",
            isPumpActive ? "ON (Pumping)" : "OFF (Idle)");
      }
    }
  }
  lastButtonReading = reading;

  // -------------------------------------------------------------
  // 2. READ SENSORS (Smoothed & Outlier Filtered)
  // -------------------------------------------------------------
  bool currentFireState = checkFlameSensor();
  if (currentFireState != isFireActive) {
    isFireActive = currentFireState;
    sendImmediateTelemetry = true; // Immediate update on flame event!
  }

  int soilRaw = readAnalogSmoothed(SOIL_PIN, 12);
  int soilPercent = calculateSoilPercentage(soilRaw);
  int rainRaw = readAnalogSmoothed(RAIN_PIN, 12);
  bool isRaining = (rainRaw > 100 && rainRaw < RAIN_THRESHOLD);

  // Periodically read DHT11 every 2 seconds to ensure fresh, non-NaN values
  if (currentMillis - lastDhtReadTime >= 2000) {
    lastDhtReadTime = currentMillis;
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    if (!isnan(temp) && !isnan(hum) && temp > -20.0 && temp < 85.0 && hum >= 0.0 && hum <= 100.0) {
      lastValidTemp = temp;
      lastValidHum = hum;
    }
  }

  // -------------------------------------------------------------
  // 3. AUTO MODE LOGIC (Emergency Firefighting + Soil Auto-Irrigation)
  // -------------------------------------------------------------
  if (autoMode) {
    // A. EMERGENCY FIRE FIGHTING:
    if (isFireActive) {
      if (!isPumpActive) {
        Serial.println("🚨 [FIRE EMERGENCY] Flame detected! Pump activated for firefighting!");
        setPumpState(true);
        sendImmediateTelemetry = true;
      }
    } else {
      // B. Normal Auto-Irrigation:
      // Turn ON if soil is dry (< 35%) and no rain
      if (!isRaining && soilPercent < 35 && soilRaw > 200) {
        if (!isPumpActive) {
          Serial.printf("[AUTO IRRIGATION] Soil dry (%d%%) -> Starting pump.\n", soilPercent);
          setPumpState(true);
          sendImmediateTelemetry = true;
        }
      }
      // Turn OFF if soil is moist (>= 70%) or rain detected
      else if (isRaining || soilPercent >= 70) {
        if (isPumpActive) {
          Serial.printf("[AUTO IRRIGATION] Soil moist (%d%%) or rain -> Stopping pump.\n", soilPercent);
          setPumpState(false);
          sendImmediateTelemetry = true;
        }
      }
    }
  }

  // -------------------------------------------------------------
  // 4. NON-BLOCKING WI-FI MONITORING & LED STATUS
  // -------------------------------------------------------------
  updateWiFiLEDs();

  if (WiFi.status() != WL_CONNECTED) {
    if (currentMillis - lastWiFiCheckTime >= WIFI_RETRY_INTERVAL) {
      lastWiFiCheckTime = currentMillis;
      Serial.println("[WiFi] Lost connection! Re-initiating connection...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  // -------------------------------------------------------------
  // 5. CONTINUOUS TELEMETRY TRANSMISSION (Heartbeat + Immediate Event)
  // -------------------------------------------------------------
  bool timeForHeartbeat = (currentMillis - lastTelemetryTime >= TELEMETRY_INTERVAL);

  if (sendImmediateTelemetry || timeForHeartbeat) {
    lastTelemetryTime = currentMillis;
    sendImmediateTelemetry = false;

    if (WiFi.status() == WL_CONNECTED) {
      sendTelemetry(lastValidTemp, lastValidHum, soilRaw, soilPercent, rainRaw,
                    isRaining, isFireActive);
    } else {
      Serial.printf("[OFFLINE] Temp: %.1fC | Hum: %.1f%% | Soil: %d%% (ADC %d) | Rain: %d | Fire: %s | Mode: %s | Pump: %s\n",
                    lastValidTemp, lastValidHum, soilPercent, soilRaw, rainRaw,
                    isFireActive ? "YES" : "NO", autoMode ? "AUTO" : "MANUAL",
                    isPumpActive ? "ON" : "OFF");
    }
  }

  delay(10); // Rapid 10ms loop cycle ensures responsive button reading
}
