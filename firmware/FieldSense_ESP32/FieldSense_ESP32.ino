/*
  =============================================================================
  FieldSense — Smart Agriculture ESP32 Node Firmware (v1.5)
  Board: ESP32 DevKit V1 (ESP-WROOM-32)
  
  Key Features:
    - 100% NON-BLOCKING: Physical manual switch & Auto Mode work anytime,
      even with NO Wi-Fi connection!
    - Active-LOW / Active-HIGH Relay Support with startup click-clack test
    - Wi-Fi Status LEDs:
        * Red LED (GPIO 19): Blinks while searching/offline
        * Blue LED (GPIO 2): Solid ON when connected
    - Auto Mode: Automated irrigation & emergency firefighting
    - Manual Mode: Web/phone control and Physical Button override on hardware
  
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
//    - When ESP32 is connected to your Laptop Mobile Hotspot (192.168.137.x):
//      Use: "http://192.168.137.1:8000"
//    - When ESP32 and Laptop are both on standard home/office Wi-Fi:
//      Use: "http://10.58.27.10:8000"
//    - For RENDER CLOUD deployment:
//      Use: "https://YOUR-SERVICE.onrender.com"
const char *SERVER_URL = "http://192.168.137.1:8000";

// 3. Secret Device Authentication Token (matches DEVICE_TOKEN on server)
const char *DEVICE_TOKEN = "FieldSenseSecretToken2026";

// ================= PIN DEFINITIONS =================
#define DHT_PIN     4   // DHT11 Data pin (Digital)
#define SOIL_PIN    34  // Capacitive soil moisture sensor analog output (ADC1_CH6)
#define RAIN_PIN    35  // Rain sensor analog output (ADC1_CH7)
#define FLAME_PIN   27  // Flame sensor digital DO output
#define PUMP_PIN    26  // 12V Pump driver (Relay IN or MOSFET Gate)
#define BUTTON_PIN  13  // Physical Pushbutton / Manual Switch (Internal pull-up to GND)

// Wi-Fi Status LED Indicators
#define LED_BLUE_PIN 2   // Blue LED (Also onboard ESP32 LED) -> ON when Wi-Fi connected
#define LED_RED_PIN  19  // Red LED -> ON/Blinking when Wi-Fi disconnected / searching

// ================= RELAY & ACTUATOR CONFIGURATION =================
// Standard 5V/3.3V Arduino relay boards are ACTIVE-LOW (LOW = Relay ON, HIGH = Relay OFF).
// If using an N-Channel MOSFET (IRLZ44N) or Active-HIGH relay, set this to false.
const bool RELAY_ACTIVE_LOW = true;

#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// ================= CALIBRATION SETTINGS =================
const int SOIL_DRY = 3100;
const int SOIL_WET = 1350;
const int RAIN_THRESHOLD = 2000;
const bool FLAME_ACTIVE_LOW = true;

// Operating Modes & States
bool autoMode     = true;   // true = Auto Mode, false = Manual Mode
bool isPumpActive = false;  // Current relay / pump state
bool isFireActive = false;

// Physical Button Debounce
int buttonState        = HIGH;
int lastButtonReading  = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 40;

// High-speed 1-Second live update interval
const unsigned long TELEMETRY_INTERVAL = 1000; // 1-second live telemetry
unsigned long lastTelemetryTime = 0;

// Non-blocking Wi-Fi reconnect timer
unsigned long lastWiFiCheckTime = 0;
const unsigned long WIFI_RETRY_INTERVAL = 15000; // Try reconnect every 15s if lost

// Non-blocking Red LED blink timer (for offline/connecting state)
unsigned long lastLedBlinkTime = 0;
bool redLedBlinkState = false;

// Cached last valid DHT readings (prevents sensor read dropouts)
float lastValidTemp = 28.5;
float lastValidHum  = 65.0;

// ================= PUMP / RELAY ACTUATOR =================
void setPumpState(bool state) {
  isPumpActive = state;

  if (RELAY_ACTIVE_LOW) {
    // Standard Active-LOW Relay: LOW = Relay energized (ON), HIGH = De-energized (OFF)
    digitalWrite(PUMP_PIN, isPumpActive ? LOW : HIGH);
  } else {
    // Active-HIGH (e.g. MOSFET): HIGH = ON, LOW = OFF
    digitalWrite(PUMP_PIN, isPumpActive ? HIGH : LOW);
  }

  Serial.printf("[RELAY] Pump -> %s | Pin %d = %s (ActiveLow=%s)\n",
                isPumpActive ? "ON (Pumping)" : "OFF (Idle)",
                PUMP_PIN,
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

// ================= SENSOR READINGS =================
int readAnalogSmoothed(int pin, int samples = 5) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(60);
  }
  return (int)(sum / samples);
}

int calculateSoilPercentage(int rawAnalog) {
  // If sensor is disconnected / floating to near zero, guard against false 100%
  if (rawAnalog < 200) return 0;
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
  if (WiFi.status() != WL_CONNECTED) return false;

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

  if (!beginSuccess) return false;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  http.setTimeout(3500); // 3.5s timeout for reliable cloud requests
  http.setReuse(true);

  StaticJsonDocument<384> doc;
  doc["temperature_c"]     = round(temp * 10.0) / 10.0;
  doc["humidity_pct"]      = round(humidity * 10.0) / 10.0;
  doc["soil_raw"]          = soilRaw;
  doc["soil_moisture_pct"] = soilPct;
  doc["rain_raw"]          = rainRaw;
  doc["raining"]           = raining;
  doc["fire_detected"]     = fire;
  doc["pump_on"]           = isPumpActive;
  doc["auto_mode"]         = autoMode;

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
          Serial.printf("[SYNC] Mode shifted from cloud -> %s\n", autoMode ? "AUTO" : "MANUAL");
        }
      }
      // If in Manual Mode, apply web dashboard pump switch
      if (!autoMode && respDoc.containsKey("pump_on")) {
        bool targetPumpState = respDoc["pump_on"] | false;
        if (targetPumpState != isPumpActive) {
          Serial.printf("[WEB COMMAND] Manual pump -> %s\n", targetPumpState ? "ON" : "OFF");
          setPumpState(targetPumpState);
        }
      }
    }
  }

  http.end();
  return (httpCode >= 200 && httpCode < 300);
}

// ================= ARDUINO SETUP =================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n==========================================");
  Serial.println("  FieldSense ESP32 Agriculture Node v1.5  ");
  Serial.println("  Non-Blocking Offline Switch + Relay Fix ");
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
  // Tests both HIGH (3.3V) and LOW (0V) so you can listen for the click!
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
  Serial.println("NOTE: If relay clicked on HIGH -> set RELAY_ACTIVE_LOW = false");
  Serial.println("NOTE: If relay clicked on LOW  -> set RELAY_ACTIVE_LOW = true");
  Serial.println("NOTE: If NO CLICK on either   -> Check 5V power to Relay VCC!");
  Serial.println("----------------------------------------------\n");

  // 5. Configure Sensor Inputs
  pinMode(FLAME_PIN, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  dht.begin();
  Serial.println("[INIT] All Sensors & Hardware Switch initialized.");

  // 6. Non-blocking Wi-Fi start (Will NOT freeze loop if Wi-Fi is off!)
  Serial.println("[WiFi] Starting connection to: " + String(WIFI_SSID));
  WiFi.mode(WIFI_STA);
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
    Serial.println("\n[WiFi] Not connected yet. Entering loop (Physical switch active offline!).");
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
        autoMode = false;       // Manual button switches to MANUAL mode immediately!
        setPumpState(!isPumpActive); // Toggle Relay ON/OFF
        Serial.printf("🔘 [PHYSICAL BUTTON PRESSED] Manual Mode -> Pump is now %s\n",
                      isPumpActive ? "ON (Pumping)" : "OFF (Idle)");
      }
    }
  }
  lastButtonReading = reading;

  // -------------------------------------------------------------
  // 2. READ SENSORS
  // -------------------------------------------------------------
  isFireActive = checkFlameSensor();
  int soilRaw = readAnalogSmoothed(SOIL_PIN, 5);
  int soilPercent = calculateSoilPercentage(soilRaw);
  int rainRaw = readAnalogSmoothed(RAIN_PIN, 5);
  bool isRaining = (rainRaw > 100 && rainRaw < RAIN_THRESHOLD);

  // -------------------------------------------------------------
  // 3. AUTO MODE LOGIC (Emergency Firefighting + Soil Auto-Irrigation)
  // -------------------------------------------------------------
  if (autoMode) {
    // A. EMERGENCY FIRE FIGHTING:
    // If flame is detected, pump starts automatically to extinguish fire!
    if (isFireActive) {
      if (!isPumpActive) {
        Serial.println("🚨 [FIRE EMERGENCY] Flame detected! Pump activated for firefighting!");
        setPumpState(true);
      }
    } else {
      // B. Normal Auto-Irrigation:
      // Turn ON if soil is dry (< 35%) and no rain
      if (!isRaining && soilPercent < 35 && soilRaw > 200) {
        if (!isPumpActive) {
          Serial.printf("[AUTO IRRIGATION] Soil dry (%d%%) -> Starting pump.\n", soilPercent);
          setPumpState(true);
        }
      }
      // Turn OFF if soil is moist (>= 70%) or rain detected
      else if (isRaining || soilPercent >= 70) {
        if (isPumpActive) {
          Serial.printf("[AUTO IRRIGATION] Soil moist (%d%%) or rain -> Stopping pump.\n", soilPercent);
          setPumpState(false);
        }
      }
    }
  }

  // -------------------------------------------------------------
  // 4. NON-BLOCKING WI-FI MONITORING & LED STATUS
  // -------------------------------------------------------------
  updateWiFiLEDs();

  if (WiFi.status() != WL_CONNECTED) {
    // Attempt non-blocking background reconnect every 15 seconds
    if (currentMillis - lastWiFiCheckTime >= WIFI_RETRY_INTERVAL) {
      lastWiFiCheckTime = currentMillis;
      Serial.println("[WiFi] Reconnecting in background...");
      WiFi.reconnect();
    }
  }

  // -------------------------------------------------------------
  // 5. PERIODIC TELEMETRY (Only runs when Wi-Fi is connected!)
  // -------------------------------------------------------------
  if (currentMillis - lastTelemetryTime >= TELEMETRY_INTERVAL) {
    lastTelemetryTime = currentMillis;

    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();

    if (!isnan(temp) && !isnan(hum)) {
      lastValidTemp = temp;
      lastValidHum  = hum;
    }

    if (WiFi.status() == WL_CONNECTED) {
      sendTelemetry(lastValidTemp, lastValidHum, soilRaw, soilPercent, rainRaw,
                    isRaining, isFireActive);
    } else {
      // Offline debug log (helps verify sensors & button in field)
      Serial.printf("[OFFLINE] Temp: %.1fC | Soil: %d%% (ADC %d) | Rain: %d | Fire: %s | Mode: %s | Pump: %s\n",
                    lastValidTemp, soilPercent, soilRaw, rainRaw,
                    isFireActive ? "YES" : "NO",
                    autoMode ? "AUTO" : "MANUAL",
                    isPumpActive ? "ON" : "OFF");
    }
  }

  delay(10); // Rapid 10ms loop cycle
}
