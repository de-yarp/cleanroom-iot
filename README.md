# Cleanroom IoT Monitor

A smart entry control and air quality monitoring system for a controlled-environment room. Built as a university IoT project at TUKE.

**Live dashboard:** [esp-cleanroom-g0crf6czagc8addj.spaincentral-01.azurewebsites.net](https://esp-cleanroom-g0crf6czagc8addj.spaincentral-01.azurewebsites.net)

---

## What it does

- Counts people entering and exiting through a doorframe using two IR sensors
- Triggers a buzzer alarm when occupancy exceeds the configured limit
- Monitors air quality (MQ-135), temperature, and humidity (DHT11)
- Automatically activates a ventilation servo when air quality drops below threshold
- Streams all sensor data to a cloud backend every 2 seconds
- Serves a real-time web dashboard with live readings, historical graphs, event log, and remote control

All automation logic runs locally on the ESP32 — if WiFi drops, alarms and ventilation continue to work.

---

## Hardware

| Component | Role |
|---|---|
| ESP32 (USB-C, CP2102) | Main controller |
| IR sensor x2 (LM393) | Directional people counting |
| MQ-135 | Air quality / VOC monitoring |
| DHT11 | Temperature + humidity |
| SG90 servo | Ventilation fan |
| Passive buzzer | Overcapacity alarm |
| Red + Green LED | Status indicator |

---

## Stack

| Layer | Technology |
|---|---|
| Firmware | Arduino C++ (ESP32 Arduino core 3.x) |
| Backend | Python, FastAPI, SQLite, aiosqlite |
| Frontend | HTML / CSS / JS, Chart.js |
| Hosting | Azure App Service (Spain Central) |
| CI/CD | GitHub Actions |

---

## Project structure

```
backend/
  routes/         # FastAPI routers: data ingestion, status, config, control
  static/         # Frontend: index.html, app.js, style.css
  database.py     # SQLite setup and connection
  models.py       # Pydantic models
  main.py         # App factory
ESP_source_code/
  cleanroom.ino   # ESP32 firmware
simulate.py       # Data simulator for backend testing without hardware
```

---

## Running locally

```bash
uv sync
uv run fastapi dev backend/main.py
```

Backend runs at `http://localhost:8000`. To simulate sensor data without hardware:

```bash
uv run python simulate.py
```

---

## ESP32 pin assignments

| Pin | Component |
|---|---|
| GPIO34 | IR sensor 1 (outer) |
| GPIO35 | IR sensor 2 (inner) |
| GPIO32 | MQ-135 AO (via voltage divider) |
| GPIO27 | DHT11 data |
| GPIO25 | SG90 servo signal |
| GPIO26 | Passive buzzer |
| GPIO12 | Red LED |
| GPIO14 | Green LED |

MQ-135 analog output (up to 5V) is divided down with two 10kΩ resistors in series before connecting to the ESP32 ADC pin (max 3.3V).

---

## API endpoints

| Method | Path | Description |
|---|---|---|
| POST | `/data` | Receive periodic sensor payload from ESP32 |
| POST | `/event` | Receive IR entry/exit event |
| GET | `/status` | Latest sensor snapshot |
| GET | `/history` | Historical data (`?range=15m/1h/6h/24h`) |
| GET | `/events` | Entry/exit event log |
| GET | `/config` | Current thresholds (polled by ESP32) |
| POST | `/config` | Update thresholds from dashboard |
| POST | `/fan` | Manual fan override (`on`/`off`/`auto`) |
| POST | `/buzzer/silence` | Silence active buzzer |
