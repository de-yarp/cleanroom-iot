"""
simulate.py — fake ESP32 for local development

Sends periodic sensor payloads and random entry/exit events to the backend.
Run with: uv run python simulate.py

Backend must be running first:
  uv run uvicorn backend.main:app --reload
"""

import random
import time

import httpx

BASE_URL = "http://127.0.0.1:8000"

# Simulation state
occupancy = 0
max_cap = 5
entry_count = 0
exit_count = 0
fan_state = "auto_off"
buzzer_state = "idle"

# Buzzer timing
buzzer_end = 0.0
cooldown_end = 0.0

# Config (synced from backend every 10s)
config = {
    "max_capacity": 5,
    "air_quality_threshold": 300,
    "buzzer_duration_s": 5,
    "cooldown_duration_s": 15,
    "fan_override": "auto",
    "buzzer_silence": 0,
}

last_config_sync = 0.0
last_sensor_post = 0.0
last_event_time = 0.0


def sync_config():
    try:
        r = httpx.get(f"{BASE_URL}/config", timeout=3)
        if r.status_code == 200:
            config.update(r.json())
            print(f"[config] synced: {config}")
    except Exception as e:
        print(f"[config] sync failed: {e}")


def send_sensor_payload(temperature, humidity, air_quality):
    payload = {
        "timestamp": int(time.time()),
        "occupancy": occupancy,
        "max_capacity": config["max_capacity"],
        "temperature": round(temperature, 1),
        "humidity": round(humidity, 1),
        "air_quality": air_quality,
        "fan_state": fan_state,
        "buzzer_state": buzzer_state,
        "entry_count": entry_count,
        "exit_count": exit_count,
    }
    try:
        r = httpx.post(f"{BASE_URL}/data", json=payload, timeout=3)
        print(f"[data]   {payload} → {r.status_code}")
    except Exception as e:
        print(f"[data]   post failed: {e}")


def send_event(event_type):
    payload = {
        "timestamp": int(time.time()),
        "event": event_type,
        "occupancy_after": occupancy,
    }
    try:
        r = httpx.post(f"{BASE_URL}/event", json=payload, timeout=3)
        print(f"[event]  {payload} → {r.status_code}")
    except Exception as e:
        print(f"[event]  post failed: {e}")


def maybe_trigger_event():
    """Randomly trigger an entry or exit every 8-20 seconds."""
    global occupancy, entry_count, exit_count

    if random.random() < 0.4:
        if occupancy > 0 and random.random() < 0.4:
            occupancy -= 1
            exit_count += 1
            send_event("exit")
        elif occupancy < 8:
            occupancy += 1
            entry_count += 1
            send_event("entry")


def update_buzzer():
    global buzzer_state, buzzer_end, cooldown_end

    now = time.time()

    # Buzzer silence one-shot from backend
    if config.get("buzzer_silence") == 1:
        buzzer_state = "idle"
        buzzer_end = 0.0
        cooldown_end = now + config["cooldown_duration_s"]
        print("[buzzer] silenced by backend")
        return

    overcapacity = occupancy > config["max_capacity"]

    if buzzer_state == "idle" and overcapacity:
        buzzer_state = "active"
        buzzer_end = now + config["buzzer_duration_s"]
        print("[buzzer] active — overcapacity")

    elif buzzer_state == "active" and now >= buzzer_end:
        buzzer_state = "cooldown"
        cooldown_end = now + config["cooldown_duration_s"]
        print("[buzzer] entering cooldown")

    elif buzzer_state == "cooldown" and now >= cooldown_end:
        buzzer_state = "idle"
        print("[buzzer] cooldown done")


def update_fan(air_quality):
    global fan_state

    override = config.get("fan_override", "auto")

    if override == "on":
        fan_state = "manual_on"
    elif override == "off":
        fan_state = "manual_off"
    else:
        if air_quality > config["air_quality_threshold"]:
            fan_state = "auto_on"
        else:
            fan_state = "auto_off"


def main():
    global last_config_sync, last_sensor_post, last_event_time

    print(f"Simulator starting — targeting {BASE_URL}")
    print("Press Ctrl+C to stop.\n")

    # Drifting sensor values for realism
    temperature = 22.0
    humidity = 45.0
    air_quality = 180

    while True:
        now = time.time()

        # Sync config every 10s
        if now - last_config_sync >= 10:
            sync_config()
            last_config_sync = now

        # Drift sensor values slightly each cycle
        temperature += random.uniform(-0.2, 0.2)
        humidity += random.uniform(-0.5, 0.5)
        air_quality += random.randint(-5, 5)

        # Occasional air quality spike to trigger fan
        if random.random() < 0.05:
            air_quality += random.randint(80, 150)
            print("[sim]    air quality spike")

        air_quality = max(50, min(600, air_quality))
        temperature = max(18.0, min(30.0, temperature))
        humidity = max(20.0, min(80.0, humidity))

        # Random entry/exit every 8-20s
        if now - last_event_time >= random.uniform(8, 20):
            maybe_trigger_event()
            last_event_time = now

        update_buzzer()
        update_fan(air_quality)

        # Send sensor payload every 2s
        if now - last_sensor_post >= 2:
            send_sensor_payload(temperature, humidity, air_quality)
            last_sensor_post = now

        time.sleep(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nSimulator stopped.")
