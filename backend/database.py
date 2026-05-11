import aiosqlite

DB_PATH = "cleanroom.db"


async def get_db() -> aiosqlite.Connection:
    db = await aiosqlite.connect(DB_PATH)
    db.row_factory = aiosqlite.Row
    return db


async def init_db():
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute("""
            CREATE TABLE IF NOT EXISTS sensor_readings (
                id            INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp     INTEGER NOT NULL,
                temperature   REAL,
                humidity      REAL,
                air_quality   INTEGER,
                occupancy     INTEGER,
                fan_state     TEXT,
                buzzer_state  TEXT,
                entry_count   INTEGER,
                exit_count    INTEGER
            )
        """)

        await db.execute("""
            CREATE TABLE IF NOT EXISTS events (
                id             INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp      INTEGER NOT NULL,
                event_type     TEXT NOT NULL,
                occupancy_after INTEGER
            )
        """)

        await db.execute("""
            CREATE TABLE IF NOT EXISTS config (
                id                    INTEGER PRIMARY KEY CHECK (id = 1),
                max_capacity          INTEGER NOT NULL DEFAULT 5,
                air_quality_threshold INTEGER NOT NULL DEFAULT 300,
                buzzer_duration_s     INTEGER NOT NULL DEFAULT 5,
                cooldown_duration_s   INTEGER NOT NULL DEFAULT 15,
                fan_override          TEXT    NOT NULL DEFAULT 'auto',
                buzzer_silence        INTEGER NOT NULL DEFAULT 0
            )
        """)

        # Ensure the single config row always exists
        await db.execute("""
            INSERT OR IGNORE INTO config (id) VALUES (1)
        """)

        await db.commit()
