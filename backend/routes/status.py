import time

from fastapi import APIRouter, HTTPException, Query

from backend.database import get_db
from backend.models import EventRecord, StatusResponse

router = APIRouter()

RANGE_SECONDS = {
    "15m": 15 * 60,
    "1h": 60 * 60,
    "6h": 6 * 60 * 60,
    "24h": 24 * 60 * 60,
}


@router.get("/status", response_model=StatusResponse)
async def get_status():
    db = await get_db()
    try:
        cursor = await db.execute(
            "SELECT * FROM sensor_readings ORDER BY timestamp DESC LIMIT 1"
        )
        row = await cursor.fetchone()
    finally:
        await db.close()

    if row is None:
        raise HTTPException(status_code=503, detail="No data yet")

    # Fetch max_capacity from config to include in response
    db = await get_db()
    try:
        cursor = await db.execute(
            "SELECT max_capacity, air_quality_threshold FROM config WHERE id = 1"
        )
        config_row = await cursor.fetchone()
    finally:
        await db.close()

    return StatusResponse(
        timestamp=row["timestamp"],
        occupancy=row["occupancy"],
        max_capacity=config_row["max_capacity"],
        temperature=row["temperature"],
        humidity=row["humidity"],
        air_quality=row["air_quality"],
        air_quality_threshold=config_row["air_quality_threshold"],
        fan_state=row["fan_state"],
        buzzer_state=row["buzzer_state"],
        entry_count=row["entry_count"],
        exit_count=row["exit_count"],
    )


@router.get("/history")
async def get_history(range: str = Query(default="1h")):
    if range not in RANGE_SECONDS:
        raise HTTPException(
            status_code=400,
            detail=f"Invalid range. Choose from: {', '.join(RANGE_SECONDS)}",
        )

    since = int(time.time()) - RANGE_SECONDS[range]

    db = await get_db()
    try:
        cursor = await db.execute(
            """
            SELECT timestamp, occupancy, air_quality, temperature, humidity
            FROM sensor_readings
            WHERE timestamp >= ?
            ORDER BY timestamp ASC
            """,
            (since,),
        )
        rows = await cursor.fetchall()
    finally:
        await db.close()

    return [dict(row) for row in rows]


@router.get("/events", response_model=list[EventRecord])
async def get_events(limit: int = Query(default=50, le=200)):
    db = await get_db()
    try:
        cursor = await db.execute(
            "SELECT * FROM events ORDER BY timestamp DESC LIMIT ?",
            (limit,),
        )
        rows = await cursor.fetchall()
    finally:
        await db.close()

    return [dict(row) for row in rows]
