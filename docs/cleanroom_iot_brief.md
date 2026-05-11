# IoT Project Brief — Clean Room Monitor & Access Controller

## Concept

A smart clean room entry system for a controlled facility environment. Monitors how many personnel are inside, tracks air quality and temperature, and enforces occupancy limits automatically. Cloud backend stores historical data for compliance logging. Frontend gives facility staff remote visibility and control.

---

## Hardware

| Component | Role | Notes | Link |
|---|---|---|---|
| ESP32 | Main controller | WiFi built in | — (from kit) |
| IR sensor x2 (LM393) | Directional people counting | Side-mounted on door frame, waist height, offset front-to-back. Reflective type — detects objects passing in front | [techfun.sk](https://techfun.sk/produkt/infracerveny-senzor-pre-vyhybanie-sa-prekazkam/) |
| MQ-135 | Air quality / VOC monitoring | Needs ~20 min warm-up before accurate readings. Powered from 5V (VIN pin). **Analog output can reach up to 5V — requires a voltage divider on the AO pin before connecting to ESP32 ADC (max 3.3V). Use two resistors e.g. 10kΩ + 20kΩ to bring output range down safely.** | [techfun.sk](https://techfun.sk/produkt/senzory-plynov-mq-x/?attribute_pa_typ-mq=mq-135) |
| DHT11 (na doske + kábliky) | Temperature + humidity | Two readings from one chip. Module version — no pull-up resistor needed | [techfun.sk](https://techfun.sk/produkt/senzor-teploty-a-vlhkosti-dht11/?attribute_pa_balenie=dht11-na-doske-kabliky) |
| DC motor + cardboard blade | Ventilation fan | From kit. Cardboard blade taped to shaft symmetrically. Driven via 2N2222 transistor — ESP32 GPIO cannot power motor directly | — (from kit) |
| 2N2222 NPN transistor | Motor driver | One transistor + one resistor switches motor from 5V rail. Rated 800mA — sufficient headroom for the motor under demo load | [techfun.sk](https://techfun.sk/produkt/bjt-npn-tranzistor-2n2222-800ma/) |
| Passive buzzer module | Occupancy alarm | Passive buzzer requires PWM tone signal to produce sound — use `ledcWriteTone()` on ESP32, not a simple HIGH/LOW | [techfun.sk](https://techfun.sk/produkt/pasivny-buzzer-na-doske/) |
| Red LED + 220Ω resistor | Overcapacity / warning status | From kit | — |
| Green LED + 220Ω resistor | Room clear / nominal status | From kit | — |

**Sensors: 3 (IR x2 + MQ-135 + DHT11)**
**Actuators: 3 (motor, buzzer, LED pair)**

---

## Physical Model

### Overview

An open-top cardboard box representing a clean room. One wall has a doorframe cutout with the two IR sensors mounted inside it. The breadboard with all components sits inside or beside the box. No roof needed — open top keeps everything accessible during demo and lets you reach in to trigger sensors manually.

### Box dimensions

Approximately **30cm wide × 20cm deep × 20cm tall**. Exact dimensions don't matter — large enough to fit a breadboard inside comfortably, small enough to sit on a desk.

### Doorframe cutout

Cut a rectangular opening in one of the **shorter walls (20cm wide side)**. Recommended cutout size: **8cm wide × 15cm tall**. This represents the clean room entry door. No actual door needed — just an opening. Personnel "enter" and "exit" by passing a hand or object through it.

### IR sensor mounting

Both IR sensors mount on the **inside surface of the same wall**, at the same height (roughly mid-height of the cutout), offset **~10cm apart horizontally along the depth of the wall** — one closer to the outside, one closer to the inside.

- IR1 — closer to outside edge (triggers first on entry)
- IR2 — closer to inside edge (triggers second on entry, first on exit)

Both sensors face across the opening at the same angle. A person or object passing through hits IR1 first then IR2 (entry) or IR2 first then IR1 (exit) — the sequence is what determines direction. Mounting them at different heights would cause unpredictable simultaneous triggering and break the directional logic.

Mount with tape or hot glue. Run jumper wires along the wall down to the breadboard.

Detection range adjustable via the blue potentiometer on each board — tune to reliably detect a hand at ~8cm distance. Test before final assembly.

### Remaining components

Everything else (breadboard, ESP32, MQ-135, DHT11, motor, buzzer, LEDs) sits inside the box or along the edge. No fixed placement required.

Motor with cardboard blade sits in a corner. Cut blade symmetrically (~5cm diameter), tape centered on shaft. Position so blade spins freely.

MQ-135 and DHT11 should be placed **away from the motor** to avoid airflow interference with readings.

### Power

Single USB cable from laptop or phone charger → ESP32. Everything runs off this. No external power supply needed.

- 3.3V pin → DHT11, IR sensors, LEDs, buzzer
- 5V (VIN) pin → MQ-135, motor (via transistor)

---

## Sensor Logic

### People counting (IR x2)
- IR1 triggers first, then IR2 → `occupancy++` (entry)
- IR2 triggers first, then IR1 → `occupancy--` (exit)
- After each valid event: **1.5 second lockout** — all IR input ignored. Prevents spam
- Occupancy cannot go below 0

### Buzzer behavior on overcapacity
- Occupancy exceeds `max_capacity` → buzzer fires for **5 seconds flat**
- After 5 seconds: **15 second cooldown** before retriggering
- During cooldown buzzer stays silent even if still overcrowded
- Both durations configurable from frontend

### Fan automation (MQ-135)
- Air quality drops below `air_quality_threshold` → motor turns on automatically
- Motor turns off when reading recovers above threshold
- Can also be toggled manually from frontend regardless of sensor state

### LEDs
- Green ON, Red OFF → occupancy below max, air quality nominal
- Red ON, Green OFF → overcapacity OR air quality critical

---

## Automation Logic Lives on ESP32

All threshold logic runs locally on the ESP32. If WiFi drops, ventilation and alarms still function. Cloud receives data but does not control actuators directly.

---

## ESP32 Implementation Notes

### Language
Arduino C++ (Arduino IDE with Espressif ESP32 board support installed via Board Manager).

### GPIO pin assignments

Define all pins as constants at the top of the sketch. Wiring must match these exactly:

```cpp
// Sensors
#define IR1_PIN     34   // ADC1 — outer IR sensor
#define IR2_PIN     35   // ADC1 — inner IR sensor
#define MQ135_PIN   32   // ADC1 only — see constraint below
#define DHT_PIN     27   // any digital GPIO

// Actuators
#define MOTOR_PIN   25   // via 2N2222 transistor
#define BUZZER_PIN  26   // PWM capable
#define LED_RED     14
#define LED_GREEN   12
```

### Libraries needed
- DHT11 → `DHT sensor library` by Adafruit (install via Library Manager)
- IR sensors → no library, plain `digitalRead()`
- MQ-135 → no library, plain `analogRead()` — see ADC pin constraint below
- Buzzer → no library, use `ledcWriteTone()` for PWM tone
- HTTP → `HTTPClient.h` built into ESP32 Arduino core

### Critical — ADC1 pins only for MQ-135
ESP32 ADC2 pins are disabled when WiFi is active — readings return garbage. MQ-135 AO pin must connect to an **ADC1 pin only**: GPIO 32, 33, 34, 35, 36, or 39. Wiring to any other analog pin will silently fail once WiFi connects.

### Critical — WiFi connectivity
Test ESP32 connectivity on the exact network used for the demo as early as possible. University networks often block unknown devices. If blocked, use a phone hotspot as fallback — prepare this in advance.

### Parallel development
ESP32 code and backend/frontend can be built simultaneously without hardware. All pin assignments and logic are defined upfront. ESP32 code can be tested by printing sensor values to Serial Monitor using dummy values before hardware arrives. Backend runs locally on MacBook during development — no Azure deployment needed until integration phase.

---

## ESP32 → Backend Communication

- Protocol: **HTTP POST**
- Frequency: every **5 seconds** for environmental sensors (DHT11, MQ-135), event-based for IR triggers
- Periodic payload (JSON):

```json
{
  "timestamp": 1714900000,
  "occupancy": 3,
  "max_capacity": 5,
  "temperature": 22.4,
  "humidity": 41.0,
  "air_quality": 187,
  "fan_state": "auto_on",
  "buzzer_state": "cooldown",
  "entry_count": 12,
  "exit_count": 9
}
```

- IR event payload (sent immediately on detection):

```json
{
  "timestamp": 1714900012,
  "event": "entry",
  "occupancy_after": 4
}
```

- ESP32 polls `GET /config` every 10 seconds to pick up threshold changes from frontend

---

## Backend

- Stack: **Python + FastAPI + SQLite**
- Hosted on **Azure App Service**, deployed via GitHub Actions
- Endpoints:
  - `POST /data` — receives periodic sensor payload from ESP32
  - `POST /event` — receives IR entry/exit event from ESP32
  - `GET /status` — latest sensor snapshot for frontend
  - `GET /history?range=1h` — historical data for graphs
  - `GET /events` — entry/exit event log
  - `GET /config` — ESP32 polls this for current thresholds
  - `POST /config` — frontend updates thresholds
  - `POST /fan` — manual fan override (on/off/auto)
  - `POST /buzzer/silence` — silence active buzzer immediately

---

## Frontend

Single-page dashboard, plain HTML/CSS/JS. Served directly from FastAPI as static files — no separate hosting needed. Lives in a `/static` folder in the same repo, deployed together with the backend to Azure. Polls `GET /status` every 3-5 seconds.

Panels:
- **Occupancy** — large live counter, color-coded (green/yellow/red), entry/exit counts
- **Air quality** — live MQ-135 reading, threshold indicator
- **Temperature / Humidity** — live DHT11 readings
- **Fan status** — current state (auto/manual/off), toggle button
- **Buzzer** — current state (active/cooldown/idle), silence button
- **Configuration panel** — max capacity, air quality threshold, buzzer duration, cooldown period
- **Event log** — timestamped entry/exit events
- **Historical graph** — occupancy and air quality over time

---

## Demo Flow

1. System boots, green LED on, dashboard shows 0 occupancy
2. Pass hand through doorframe → IR sequence detected → count increments on dashboard
3. Pass hand back → count decrements
4. Repeat until max capacity hit → buzzer fires 5 seconds, red LED on
5. Silence buzzer from frontend → manual actuator control demonstrated
6. Breathe on MQ-135 → air quality drops → fan kicks on automatically
7. Lower max capacity from frontend → retrigger alarm without touching hardware
8. Show event log with timestamped entries/exits
