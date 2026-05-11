from fastapi import APIRouter

from backend.database import get_db
from backend.models import FanControl

router = APIRouter()


@router.post("/fan")
async def set_fan(payload: FanControl):
    db = await get_db()
    try:
        await db.execute(
            "UPDATE config SET fan_override = ? WHERE id = 1",
            (payload.state,),
        )
        await db.commit()
    finally:
        await db.close()

    return {"ok": True, "fan_override": payload.state}


@router.post("/buzzer/silence")
async def silence_buzzer():
    db = await get_db()
    try:
        await db.execute("UPDATE config SET buzzer_silence = 1 WHERE id = 1")
        await db.commit()
    finally:
        await db.close()

    return {"ok": True}
