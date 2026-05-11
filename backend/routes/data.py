from fastapi import APIRouter

from backend.database import get_db
from backend.models import EventPayload, SensorPayload

router = APIRouter()


@router.post("/data")
async def receive_sensor_data(payload: SensorPayload):
    db = await get_db()
    try:
        await db.execute(
            """
            INSERT INTO sensor_readings
                (timestamp, temperature, humidity, air_quality, occupancy,
                 fan_state, buzzer_state, entry_count, exit_count)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                payload.timestamp,
                payload.temperature,
                payload.humidity,
                payload.air_quality,
                payload.occupancy,
                payload.fan_state,
                payload.buzzer_state,
                payload.entry_count,
                payload.exit_count,
            ),
        )
        await db.commit()
    finally:
        await db.close()

    return {"ok": True}


@router.post("/event")
async def receive_event(payload: EventPayload):
    db = await get_db()
    try:
        await db.execute(
            """
            INSERT INTO events (timestamp, event_type, occupancy_after)
            VALUES (?, ?, ?)
            """,
            (payload.timestamp, payload.event, payload.occupancy_after),
        )
        await db.commit()
    finally:
        await db.close()

    return {"ok": True}
