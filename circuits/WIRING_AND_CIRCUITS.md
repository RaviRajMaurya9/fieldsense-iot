# FieldSense Hardware Wiring & Circuit Schematics

This document provides exact pinouts, connection schematics, and electrical protection rules for building the **FieldSense Smart Agriculture Monitoring & Irrigation System**.

---

## 1. System Pinout Map (ESP32 DevKit V1 - 30/36 Pin)

| Sensor / Actuator | Module Pin | ESP32 Pin | Signal Type | Electrical Notes |
| :--- | :--- | :--- | :--- | :--- |
| **DHT11** | VCC | **3.3V** | Power | Connect to 3.3V rail |
| | GND | **GND** | Ground | Common ground |
| | DATA | **GPIO 4** | Digital I/O | If raw 4-pin DHT11 is used, add 10kΩ pullup to 3.3V |
| **Capacitive Soil Moisture** | VCC | **3.3V** | Power | Power from 3.3V for linear ADC output |
| | GND | **GND** | Ground | Common ground |
| | AOUT | **GPIO 34** | Analog Input | **ADC1_CH6** (Input-only pin on ESP32) |
| **Rain Sensor Module** | VCC | **3.3V** | Power | Use 3.3V to ensure analog out ≤ 3.3V |
| | GND | **GND** | Ground | Common ground |
| | AO (Analog) | **GPIO 35** | Analog Input | **ADC1_CH7** (Input-only pin on ESP32) |
| **Flame Sensor Module** | VCC | **3.3V** | Power | Connect to 3.3V |
| | GND | **GND** | Ground | Common ground |
| | DO (Digital) | **GPIO 27** | Digital Input | Active LOW when fire detected |
| **Physical Manual Switch** | Pin 1 | **GPIO 13** | Digital Input | Internal Pull-Up (`INPUT_PULLUP`). Active LOW when pressed |
| | Pin 2 | **GND** | Ground | Common ground |
| **Wi-Fi Status Blue LED** | Anode (+) | **GPIO 2** | Digital Output | Series 220Ω resistor. Solid ON when Wi-Fi connected (also onboard LED) |
| | Cathode (-) | **GND** | Ground | Common ground |
| **Wi-Fi Status Red LED** | Anode (+) | **GPIO 19** | Digital Output | Series 220Ω resistor. Blinking/ON when disconnected or offline |
| | Cathode (-) | **GND** | Ground | Common ground |
| **12V Pump Driver** | Control In | **GPIO 26** | Digital Output | Drives MOSFET Gate or Relay module IN (Active-LOW) |

> [!CAUTION]
> **ADC Voltage Safety Warning:**
> - ESP32 analog pins (**GPIO 34 and GPIO 35**) support an absolute maximum input of **3.3V**!
> - Never power analog sensors from 5V without a voltage divider (e.g., 1kΩ / 2kΩ) or you will permanently damage the ESP32 ADC channel. Powering sensor modules from the ESP32's **3.3V pin** guarantees the output voltage never exceeds 3.3V.

---

## 2. 12V DC Water Pump Driver Circuit

Driving a 12V DC inductive load requires switching isolation and inductive kickback protection.

### Option A: Logic-Level N-Channel MOSFET (Recommended: IRLZ44N or FQP30N06L)

```
                 +12V DC Supply (Pump Power)
                      |
                      +-------------------+
                      |                   |
                  [ 12V Pump ]         [ 1N4007 Diode ]
                      |              (Cathode / Stripe to +12V)
                      +-------------------+
                      |
                   |--+ (Drain)
  ESP32 GPIO 26 ---|    (IRLZ44N MOSFET)
         |         |<-+ (Source)
       [100Ω]         |
         |            |
  (Gate) +            |
         |            |
      [100kΩ]         |
         |            |
        GND ---------+------------------- GND (12V Supply & ESP32 Common Ground)
```

#### Component Breakdown:
1. **MOSFET:** Must be **Logic-Level** (starts with `IRL...` or rated for $V_{GS(th)} \le 2.5\text{V}$), so 3.3V from ESP32 GPIO 26 can fully saturate the channel.
2. **Flyback Diode (1N4007 or 1N5819):** Placed in reverse parallel across the pump motor terminals (Cathode with stripe towards +12V). This absorbs massive inductive voltage spikes when the motor stops.
3. **Gate Resistor (100Ω):** Limits high-frequency inrush current into the MOSFET gate capacitor to protect GPIO 26.
4. **Pulldown Resistor (100kΩ):** Connects between Gate and GND to keep the pump firmly OFF when the ESP32 boots or resets.
5. **Common Ground:** The negative terminal of the 12V power supply **MUST** be connected to the ESP32 GND pin.

---

### Option B: 5V / 3.3V Optocoupled Relay Module

If using a standard pre-assembled relay board (e.g. Songle blue relay module):
1. **VCC:** Connect to **5V / Vin** (Relay coils need 5V to switch firmly; powering from 3.3V often causes weak or failed switching).
2. **GND:** Connect to ESP32 **GND**.
3. **IN:** Connect to ESP32 **GPIO 26**.
4. **Relay Terminal Wiring (Output):**
   - **COM (Common):** Connect to +12V Power Supply Positive.
   - **NO (Normally Open):** Connect to Water Pump Positive (+).
   - **Pump Negative (-):** Connect directly to 12V Power Supply GND.
   - *(Do NOT use the NC / Normally Closed terminal, or the pump will run in reverse!)*

> [!IMPORTANT]
> **Active-LOW vs Active-HIGH Relay Switching:**
> - Most Arduino relay boards are **Active-LOW** (Signal `LOW` 0V = ON, Signal `HIGH` 3.3V = OFF).
> - However, some modules are **Active-HIGH** or have an **`H / L` Jumper**:
>   - If your module has a yellow jumper labeled **H / L**: Move it to **L** (for Active-LOW) or **H** (for Active-HIGH).
>   - In firmware line 48:
>     * If relay turns ON when pin is LOW $\rightarrow$ `const bool RELAY_ACTIVE_LOW = true;`
>     * If relay turns ON when pin is HIGH $\rightarrow$ `const bool RELAY_ACTIVE_LOW = false;`
>
> **The 5-Second Wire Touch Test (Prove Hardware Power & Trigger Type):**
> Disconnect the wire from ESP32 Pin D26 and touch it directly:
> 1. Touch wire to **GND**: If relay clicks ON $\rightarrow$ it is **Active-LOW**.
> 2. Touch wire to **VIN (5V)**: If relay clicks ON $\rightarrow$ it is **Active-HIGH**.
> 3. If it **NEVER clicks** in either case $\rightarrow$ **The relay has NO 5V power!**
>    - Ensure VCC is plugged into **VIN (5V)**, NOT 3.3V.
>    - Ensure GND is firmly connected to ESP32 **GND**.
>    - Ensure the `JD-VCC / VCC` jumper cap is installed on the relay board.

---

## 3. Wi-Fi Status LED Indicators (Red & Blue)

To visually see connection status without a computer:

```
ESP32 DevKit V1
   +---------+
   |  GPIO 2 | ----[ 220Ω Resistor ]----(Anode +) [ BLUE LED ] (Cathode -) ---- GND
   |         |   (Note: Also drives the onboard blue LED on ESP32)
   | GPIO 19 | ----[ 220Ω Resistor ]----(Anode +) [  RED LED ] (Cathode -) ---- GND
   +---------+
```

### LED Behavior Map:
| Status | Red LED (GPIO 19) | Blue LED (GPIO 2) | Meaning |
| :--- | :--- | :--- | :--- |
| **Connecting / Searching** | **Blinking (500ms)** | OFF | Searching and connecting to Wi-Fi SSID |
| **Connected & Online** | OFF | **Solid ON** | Wi-Fi connected, sending live telemetry |
| **Connection Lost / Offline**| **Blinking (500ms)** | OFF | Offline mode (Physical switch & Auto Mode fully active!) |

---

## 4. Power Supply Architecture

To avoid brownouts and sensor reading noise:

```
[ 12V 2A-3A DC Adapter ]
       |
       +---------> Powers 12V DC Water Pump (via MOSFET/Relay)
       |
       +---------> [ LM2596 / 7805 Buck Converter (12V to 5V) ]
                         |
                         +---> 5V Vin of ESP32 DevKit V1
                         |
                         +---> 5V pin of ESP32-CAM module
```

1. **Do NOT power the water pump from the ESP32's 5V or 3.3V pin.** The motor's stall/startup current will immediately reset the microcontrollers.
2. **ESP32-CAM Power:** The camera module requires up to 450mA peaks during Wi-Fi transmission. Ensure the 5V supply is stable and clean.

---

## 5. Physical Pushbutton / Manual Motor Override Switch

To operate the water pump in the field when no phone, laptop, or internet connection is available:

```
ESP32 DevKit V1
   +---------+
   | GPIO 13 | ----[ Terminal 1 ]--+
   |         |                     |  (Normally Open Pushbutton / Toggle Switch)
   |     GND | ----[ Terminal 2 ]--+
   +---------+
```

### How it works:
1. **Zero External Resistors Needed:** The ESP32 firmware configures GPIO 13 with the internal pull-up resistor (`pinMode(BUTTON_PIN, INPUT_PULLUP)`). GPIO 13 stays HIGH ($3.3\text{V}$) under normal conditions.
2. **Press to Toggle:** When the button is pressed, it shorts GPIO 13 directly to GND (LOW).
3. **Immediate Local Priority & Auto-Shift to Manual Mode:**
   - Pressing the physical button immediately shifts the system from **Auto Mode** into **Manual Mode**.
   - It toggles the pump ON or OFF instantaneously with built-in 50ms software debouncing.
   - The status is synchronized back to the web dashboard as soon as the ESP32 completes its next telemetry poll.
4. **Emergency Fire Operation:**
   - In **Auto Mode**, if fire is sensed by the flame sensor on GPIO 27, the pump automatically starts to spray water and extinguish the fire.
   - In **Manual Mode** or via the physical button, the farmer retains 100% manual control to turn the motor ON or OFF at will without system lockouts.
