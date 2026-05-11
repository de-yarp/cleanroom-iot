from fastapi import APIRouter, HTTPException

from backend.database import get_db
from backend.models import ConfigResponse, ConfigUpdate

router = APIRouter()


@router.get("/config", response_model=ConfigResponse)
async def get_config():
    db = await get_db()
    try:
        cursor = await db.execute("SELECT * FROM config WHERE id = 1")
        row = await cursor.fetchone()
    finally:
        await db.close()

    if row is None:
        raise HTTPException(
            status_code=500, detail="Config row missing — run init_db()"
        )

    # Auto-reset buzzer_silence after serving it as 1 — it's a one-shot flag.
    # ESP32 reads it, acts on it once, and the next poll sees 0 again.
    if row["buzzer_silence"] == 1:
        db = await get_db()
        try:
            await db.execute("UPDATE config SET buzzer_silence = 0 WHERE id = 1")
            await db.commit()
        finally:
            await db.close()

    return ConfigResponse(
        max_capacity=row["max_capacity"],
        air_quality_threshold=row["air_quality_threshold"],
        buzzer_duration_s=row["buzzer_duration_s"],
        cooldown_duration_s=row["cooldown_duration_s"],
        fan_override=row["fan_override"],
        buzzer_silence=row["buzzer_silence"],
    )


@router.post("/config", response_model=ConfigResponse)
async def update_config(payload: ConfigUpdate):
    if payload.max_capacity < 1:
        raise HTTPException(status_code=422, detail="max_capacity must be at least 1")
    if payload.air_quality_threshold < 0:
        raise HTTPException(
            status_code=422, detail="air_quality_threshold cannot be negative"
        )
    if payload.buzzer_duration_s < 1:
        raise HTTPException(
            status_code=422, detail="buzzer_duration_s must be at least 1"
        )
    if payload.cooldown_duration_s < 1:
        raise HTTPException(
            status_code=422, detail="cooldown_duration_s must be at least 1"
        )

    db = await get_db()
    try:
        await db.execute(
            """
            UPDATE config SET
                max_capacity          = ?,
                air_quality_threshold = ?,
                buzzer_duration_s     = ?,
                cooldown_duration_s   = ?
            WHERE id = 1
            """,
            (
                payload.max_capacity,
                payload.air_quality_threshold,
                payload.buzzer_duration_s,
                payload.cooldown_duration_s,
            ),
        )
        await db.commit()

        cursor = await db.execute(
            "SELECT fan_override, buzzer_silence FROM config WHERE id = 1"
        )
        row = await cursor.fetchone()
    finally:
        await db.close()

    return ConfigResponse(
        max_capacity=payload.max_capacity,
        air_quality_threshold=payload.air_quality_threshold,
        buzzer_duration_s=payload.buzzer_duration_s,
        cooldown_duration_s=payload.cooldown_duration_s,
        fan_override=row["fan_override"],
        buzzer_silence=row["buzzer_silence"],
    )
