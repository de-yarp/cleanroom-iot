// ============================================================
//  Clean Room Monitor & Access Controller
//  ESP32 firmware — Arduino C++
//  Board: ESP32 Dev Module (Espressif via Board Manager)
//
//  Required libraries (install via Library Manager):
//    - "DHT sensor library" by Adafruit
//    - "ArduinoJson"        by Benoit Blanchon
//
//  Motor: SG90 servo — PWM via LEDC
//
//  MQ-135 AO: voltage divider REQUIRED before ADC pin
//         10kΩ from AO to GPIO32, 20kΩ from GPIO32 to GND
//         Brings 0–5V sensor output down to 0–3.3V safe range
//
//  WiFi note: test on the exact demo network early.
//         University networks often block unknown devices.
//         Have a phone hotspot ready as fallback.
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ------------------------------------------------------------
//  WiFi & Backend config — fill in before flashing
// ------------------------------------------------------------
const char* WIFI_SSID     = "iPhone";
const char* WIFI_PASSWORD = "123456789098";
const char* BACKEND_URL   = "https://esp-cleanroom-g0crf6czagc8addj.spaincentral-01.azurewebsites.net";

// ------------------------------------------------------------
//  Pin assignments — wiring MUST match these exactly
// ------------------------------------------------------------
#define IR1_PIN      35   // ADC1 — outer IR sensor (triggers first on entry)
#define IR2_PIN      34   // ADC1 — inner IR sensor (triggers second on entry)
#define MQ135_PIN    32   // ADC1 ONLY — ADC2 is dead when WiFi is active
#define DHT_PIN      15  // DHT11 data

// Servo motor SG90 — single PWM pin
#define SERVO_PIN    25

#define BUZZER_PIN   26   // Passive buzzer — PWM via ledcWriteTone(), NOT digitalWrite()
#define LED_RED      12
#define LED_GREEN    14

// ------------------------------------------------------------
//  DHT11
// ------------------------------------------------------------
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// ------------------------------------------------------------
// SG90 servo — controlled via ESP32 LEDC PWM
// 50Hz, duty: ~1ms (off/closed) to ~2ms (on/open)
// Using LEDC channel separate from buzzer
void servoWrite(int angle) {
  // angle 0-180, maps to ~500us-~2400us pulse at 50Hz (20ms period)
  // LEDC 16-bit resolution: 65535 ticks = 20ms
  // 500us  = 65535 * 0.5/20  = ~1638
  // 2400us = 65535 * 2.4/20  = ~7864
  int duty = map(angle, 0, 180, 1638, 7864);
  ledcWrite(SERVO_PIN, duty);
}

void servoOn()  { servoWrite(180);  }  // spin position
void servoOff() { servoWrite(0);   }  // rest position

// ------------------------------------------------------------
//  MQ-135 warm-up
//  Sensor needs ~20 min after power-on before readings are
//  accurate. Fan auto-mode and LED alert logic treat the
//  sensor as non-critical until the flag clears.
//  Data is still posted to /data the whole time.
// ------------------------------------------------------------
//const unsigned long MQ135_WARMUP_MS = 20UL * 60UL * 1000UL;  // 20 minutes
// const unsigned long MQ135_WARMUP_MS = 2UL * 60UL * 1000UL;
const unsigned long MQ135_WARMUP_MS = 0;
bool mq135_ready = false;

// ------------------------------------------------------------
//  Configurable thresholds — updated from /config every 10s
// ------------------------------------------------------------
int  max_capacity        = 5;
int  air_quality_thresh  = 400;
int  buzzer_duration_ms  = 5000;
int  buzzer_cooldown_ms  = 15000;

// ------------------------------------------------------------
//  Runtime state
// ------------------------------------------------------------
int  occupancy    = 0;
int  entry_count  = 0;
int  exit_count   = 0;

// IR directional detection
bool          ir1_triggered    = false;
bool          ir2_triggered    = false;
unsigned long ir_lockout_until = 0;
const unsigned long IR_LOCKOUT_MS = 3000;

// Buzzer FSM
enum BuzzerState { BUZZER_IDLE, BUZZER_ACTIVE, BUZZER_COOLDOWN };
BuzzerState   buzzer_state       = BUZZER_IDLE;
unsigned long buzzer_state_until = 0;
bool          buzzer_silenced    = false;

// Fan
enum FanMode { FAN_AUTO, FAN_ON, FAN_OFF };
FanMode fan_mode    = FAN_AUTO;
bool    fan_running = false;

// Sensor readings
float temperature = 0.0;
float humidity    = 0.0;
int   air_quality = 0;

// Timing
unsigned long last_sensor_post  = 0;
unsigned long last_config_fetch = 0;
const unsigned long SENSOR_INTERVAL_MS = 2000;
const unsigned long CONFIG_INTERVAL_MS = 10000;

// FreeRTOS queue for IR events → HTTP task
QueueHandle_t ir_event_queue;

// ============================================================
//  Forward declarations
// ============================================================
void connectWiFi();
void httpTask(void* param);
void handleIR();
void resetIR(unsigned long now);
void handleBuzzer(unsigned long now);
void buzzerOn();
void buzzerOff();
const char* buzzerStateStr();
void servoWrite(int angle);
void servoOn();
void servoOff();
void readSensors();
void handleFan();
const char* fanStateStr();
void updateLEDs();
void postSensorData();
void postIREvent(const char* event_type);
void fetchConfig();

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Clean Room Monitor booting ===");

  pinMode(IR1_PIN, INPUT);
  pinMode(IR2_PIN, INPUT);

  ledcAttach(SERVO_PIN, 50, 16);  // 50Hz, 16-bit resolution
  servoOff();

  pinMode(LED_RED,   OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  ledcAttach(BUZZER_PIN, 2000, 8);
  ledcWrite(BUZZER_PIN, 0);

  digitalWrite(LED_RED,   LOW);
  digitalWrite(LED_GREEN, HIGH);  // green on at boot — nominal state

  dht.begin();

  //fan_running = true;

  connectWiFi();
  configTime(0, 0, "pool.ntp.org");  // sync time via NTP

  delay(5000);  // wait for NTP sync

  ir_event_queue = xQueueCreate(8, 8);  // 8 slots, 8 bytes each
  xTaskCreatePinnedToCore(httpTask, "http", 8192, NULL, 1, NULL, 0);  // core 0

  Serial.printf("[MQ135] Warm-up started — readings trusted after %.0f min\n",
                (float)MQ135_WARMUP_MS / 60000.0);
  Serial.println("=== Boot complete ===\n");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  if (!mq135_ready && now >= MQ135_WARMUP_MS) {
    mq135_ready = true;
    Serial.println("[MQ135] Warm-up complete — readings now trusted");
  }

  handleIR();
  handleBuzzer(now);
  readSensors();
  handleFan();
  updateLEDs();

  // servo is set once in handleFan — no per-loop advance needed

  delay(1);
}

// ============================================================
//  HTTP TASK — runs on core 0, loop() on core 1
//  Handles all blocking HTTP so loop() never stalls
// ============================================================
void httpTask(void* param) {
  char ir_event[8];
  while (true) {
    unsigned long now = millis();

    // IR event — immediate, from queue
    if (xQueueReceive(ir_event_queue, ir_event, 0) == pdTRUE) {
      postIREvent(ir_event);
    }

    // Periodic sensor POST
    if (now - last_sensor_post >= SENSOR_INTERVAL_MS) {
      last_sensor_post = now;
      postSensorData();
    }

    // Periodic config GET
    if (now - last_config_fetch >= CONFIG_INTERVAL_MS) {
      last_config_fetch = now;
      fetchConfig();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================================
//  WiFi
// ============================================================
void connectWiFi() {
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi FAILED — running offline mode. Automation still active.");
  }
}

// ============================================================
//  IR SENSOR — directional people counting
//  IR1 first → IR2 : entry (occupancy++)
//  IR2 first → IR1 : exit  (occupancy--)
//  1.5s lockout after each valid event to prevent spam
// ============================================================
enum SystemState {
    IDLE,
    ENTERING_STEP_1, // IR1 hit
    ENTERING_STEP_2, // IR1 hit -> IR2 hit
    EXITING_STEP_1,  // IR2 hit
    EXITING_STEP_2   // IR2 hit -> IR1 hit
};

void handleIR() {
    unsigned long now = millis();
    if (now < ir_lockout_until) return;

    bool ir1 = (digitalRead(IR1_PIN) == LOW);
    bool ir2 = (digitalRead(IR2_PIN) == LOW);

    static SystemState state = IDLE;
    static unsigned long state_start = 0;

    static unsigned long last_debug = 0;
    if ((ir1 || ir2) && (millis() - last_debug >= 200)) {
        last_debug = millis();
        Serial.printf("[IR_DBG] ir1=%d ir2=%d state=%d lockout=%lu\n", ir1, ir2, (int)state, ir_lockout_until > millis() ? ir_lockout_until - millis() : 0UL);
    }

    // Global Timeout to prevent stuck states
    if (state != IDLE && (now - state_start > 3000)) {
        state = IDLE;
    }

    switch (state) {
        case IDLE:
            // Strict check: One MUST be low while the other is HIGH
            if (ir1 && !ir2) {
                state = ENTERING_STEP_1;
                state_start = now;
            } else if (ir2 && !ir1) {
                state = EXITING_STEP_1;
                state_start = now;
            }
            break;

        case ENTERING_STEP_1:
            // Waiting for IR2 to be triggered while IR1 is still (or was) active
            if (ir2) {
                state = ENTERING_STEP_2;
            }
            break;

        case ENTERING_STEP_2: {
            occupancy++;
            entry_count++;
            Serial.printf("[IR] ENTRY — occupancy now %d\n", occupancy);
            char ev[] = "entry";
            xQueueSend(ir_event_queue, ev, 0);
            resetIR(now);
            state = IDLE;
            break;
        }
        case EXITING_STEP_1:
            if (ir1) {
                state = EXITING_STEP_2;
            }
            break;

        case EXITING_STEP_2: {
            if (occupancy > 0) occupancy--;
            exit_count++;
            Serial.printf("[IR] EXIT — occupancy now %d\n", occupancy);
            char ev[] = "exit";
            xQueueSend(ir_event_queue, ev, 0);
            resetIR(now);
            state = IDLE;
            break;
        }
    }
}

void resetIR(unsigned long now) {
  ir1_triggered    = false;
  ir2_triggered    = false;
  ir_lockout_until = now + IR_LOCKOUT_MS;
  buzzer_silenced  = false;
  // reset trigger timer — defined as static in handleIR, workaround via flag
}

// ============================================================
//  BUZZER FSM
//  IDLE     → overcapacity triggers ACTIVE for buzzer_duration_ms
//  ACTIVE   → times out or silenced → COOLDOWN for buzzer_cooldown_ms
//  COOLDOWN → times out → back to IDLE
// ============================================================
void handleBuzzer(unsigned long now) {
  bool overcapacity = (occupancy > max_capacity);

  switch (buzzer_state) {
    case BUZZER_IDLE:
      if (overcapacity && !buzzer_silenced) {
        buzzerOn();
        buzzer_state       = BUZZER_ACTIVE;
        buzzer_state_until = now + buzzer_duration_ms;
        Serial.println("[BUZZER] ACTIVE — overcapacity");
      }
      break;

    case BUZZER_ACTIVE:
      if (buzzer_silenced || now >= buzzer_state_until) {
        buzzerOff();
        buzzer_state       = BUZZER_COOLDOWN;
        buzzer_state_until = now + buzzer_cooldown_ms;
        Serial.println("[BUZZER] COOLDOWN");
      }
      break;

    case BUZZER_COOLDOWN:
      if (now >= buzzer_state_until) {
        buzzer_state    = BUZZER_IDLE;
        buzzer_silenced = false;
        Serial.println("[BUZZER] IDLE");
      }
      break;
  }
}

void buzzerOn()  { ledcWriteTone(BUZZER_PIN, 2000); }
void buzzerOff() { ledcWriteTone(BUZZER_PIN, 0); }

const char* buzzerStateStr() {
  switch (buzzer_state) {
    case BUZZER_ACTIVE:   return "active";
    case BUZZER_COOLDOWN: return "cooldown";
    default:              return "idle";
  }
}

// ============================================================
//  SENSORS
// ============================================================
void readSensors() {
  static unsigned long last_dht = 0;
  unsigned long now = millis();
  if (now - last_dht >= 2000) {
    last_dht = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) temperature = t;
    if (!isnan(h)) {
      humidity = h;
      Serial.printf("[DHT] %.1f°C  %.0f%%\n", temperature, humidity);
    } else {
      Serial.println("[DHT] NaN — check wiring on GPIO27");
    }
  }
  // MQ-135: AO through voltage divider (10kΩ+10kΩ) → GPIO32
  // Raw ADC value 0–4095 — backend interprets the scale
  air_quality = analogRead(MQ135_PIN);
}

// ============================================================
//  FAN (stepper motor — non-blocking, stepped in loop())
//  fan_mode comes from /config fan_override field:
//    "on"   → FAN_ON  : run regardless of sensor
//    "off"  → FAN_OFF : stop regardless of sensor
//    "auto" → FAN_AUTO: run when air_quality > threshold
//                       (ignored during MQ-135 warm-up)
// ============================================================
void handleFan() {
  bool should_run;
  switch (fan_mode) {
    case FAN_ON:  should_run = true;  break;
    case FAN_OFF: should_run = false; break;
    default:
      should_run = mq135_ready && (air_quality > air_quality_thresh);
      break;
  }

  if (should_run != fan_running) {
    fan_running = should_run;
    if (fan_running) servoOn(); else servoOff();
    Serial.printf("[FAN] %s (mode=%s, aq=%d, mq_ready=%s)\n",
      fan_running ? "ON" : "OFF",
      fan_mode == FAN_AUTO ? "auto" : (fan_mode == FAN_ON ? "manual_on" : "manual_off"),
      air_quality,
      mq135_ready ? "yes" : "no");
  }
}

const char* fanStateStr() {
  if (!fan_running) return fan_mode == FAN_OFF ? "manual_off" : "auto_off";
  return fan_mode == FAN_ON ? "manual_on" : "auto_on";
}

// ============================================================
//  LEDs
//  Red  → overcapacity OR air quality critical (sensor trusted)
//  Green→ nominal
// ============================================================
void updateLEDs() {
  bool aq_alert = mq135_ready && (air_quality > air_quality_thresh);
  bool alert    = (occupancy > max_capacity) || aq_alert;
  digitalWrite(LED_RED,   alert ? HIGH : LOW);
  digitalWrite(LED_GREEN, alert ? LOW  : HIGH);
}

// ============================================================
//  HTTP — POST /data  (every 2s)
// ============================================================
void postSensorData() {
  if (WiFi.status() != WL_CONNECTED) return;

  StaticJsonDocument<256> doc;
  doc["timestamp"]    = (unsigned long)time(nullptr);
  doc["occupancy"]    = occupancy;
  doc["max_capacity"] = max_capacity;
  doc["temperature"]  = temperature;
  doc["humidity"]     = humidity;
  doc["air_quality"]  = air_quality;
  doc["fan_state"]    = fanStateStr();
  doc["buzzer_state"] = buzzerStateStr();
  doc["entry_count"]  = entry_count;
  doc["exit_count"]   = exit_count;

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(String(BACKEND_URL) + "/data");
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  Serial.printf("[HTTP] POST /data → %d\n", code);
  http.end();
}

// ============================================================
//  HTTP — POST /event  (immediate on IR detection)
// ============================================================
void postIREvent(const char* event_type) {
  if (WiFi.status() != WL_CONNECTED) return;

  StaticJsonDocument<128> doc;
  doc["timestamp"]       = (unsigned long)time(nullptr);
  doc["event"]           = event_type;
  doc["occupancy_after"] = occupancy;

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(String(BACKEND_URL) + "/event");
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  Serial.printf("[HTTP] POST /event (%s) → %d\n", event_type, code);
  http.end();
}

// ============================================================
//  HTTP — GET /config  (every 10s)
//  Picks up threshold and override changes from the dashboard
// ============================================================
void fetchConfig() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(BACKEND_URL) + "/config");
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, payload)) {
      max_capacity        = doc["max_capacity"]          | max_capacity;
      air_quality_thresh  = doc["air_quality_threshold"] | air_quality_thresh;
      // Explicitly cast to prevent overflow and bitwise corruption
      buzzer_duration_ms = doc["buzzer_duration_s"].is<int>() 
                          ? (doc["buzzer_duration_s"].as<uint32_t>() * 1000) 
                          : buzzer_duration_ms;

      buzzer_cooldown_ms = doc["cooldown_duration_s"].is<int>() 
                          ? (doc["cooldown_duration_s"].as<uint32_t>() * 1000) 
                          : buzzer_cooldown_ms;

      const char* fm = doc["fan_override"] | "auto";
      if      (strcmp(fm, "on")  == 0) fan_mode = FAN_ON;
      else if (strcmp(fm, "off") == 0) fan_mode = FAN_OFF;
      else                             fan_mode = FAN_AUTO;

      // buzzer_silence = 1 from frontend → silence active buzzer immediately
      if (doc["buzzer_silence"] | false) buzzer_silenced = true;

      Serial.printf("[CONFIG] cap=%d aq_thresh=%d buz_dur=%dms buz_cool=%dms fan=%s\n",
        max_capacity, air_quality_thresh,
        buzzer_duration_ms, buzzer_cooldown_ms, fm);
    }
  } else {
    Serial.printf("[HTTP] GET /config → %d\n", code);
  }
  http.end();
}
