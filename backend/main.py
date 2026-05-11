from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles

from backend.database import init_db
from backend.routes import config, control, data, status


@asynccontextmanager
async def lifespan(app: FastAPI):
    await init_db()
    yield


app = FastAPI(title="Cleanroom IoT", lifespan=lifespan)

app.include_router(data.router)
app.include_router(status.router)
app.include_router(config.router)
app.include_router(control.router)

app.mount("/", StaticFiles(directory="backend/static", html=True), name="static")
