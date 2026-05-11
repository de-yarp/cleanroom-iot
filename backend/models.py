from typing import Literal

from pydantic import BaseModel

# ---------------------------------------------------------------------------
# ESP32 → Backend (incoming)
# ---------------------------------------------------------------------------


class SensorPayload(BaseModel):
    timestamp: int
    occupancy: int
    max_capacity: int
    temperature: float
    humidity: float
    air_quality: int
    fan_state: Literal["auto_on", "auto_off", "manual_on", "manual_off"]
    buzzer_state: Literal["active", "cooldown", "idle"]
    entry_count: int
    exit_count: int


class EventPayload(BaseModel):
    timestamp: int
    event: Literal["entry", "exit"]
    occupancy_after: int


# ---------------------------------------------------------------------------
# Frontend → Backend (incoming)
# ---------------------------------------------------------------------------


class ConfigUpdate(BaseModel):
    max_capacity: int
    air_quality_threshold: int
    buzzer_duration_s: int
    cooldown_duration_s: int


class FanControl(BaseModel):
    state: Literal["on", "off", "auto"]


# ---------------------------------------------------------------------------
# Backend → Frontend (outgoing)
# ---------------------------------------------------------------------------


class StatusResponse(BaseModel):
    timestamp: int
    occupancy: int
    max_capacity: int
    temperature: float
    humidity: float
    air_quality: int
    fan_state: str
    buzzer_state: str
    entry_count: int
    exit_count: int


class EventRecord(BaseModel):
    id: int
    timestamp: int
    event_type: str
    occupancy_after: int


class ConfigResponse(BaseModel):
    max_capacity: int
    air_quality_threshold: int
    buzzer_duration_s: int
    cooldown_duration_s: int
    fan_override: Literal["on", "off", "auto"]
    buzzer_silence: int  # 0 or 1 — one-shot flag, reset to 0 after ESP32 reads it
