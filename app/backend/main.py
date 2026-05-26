import asyncio
import json
import logging
from contextlib import asynccontextmanager
from pathlib import Path

import uvicorn
import yaml
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from mqtt_client import MqttClient
from settings import Settings
from store import EventStore

logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
logger = logging.getLogger(__name__)

settings = Settings()
store = EventStore()
ws_clients: set[WebSocket] = set()
shoes: dict[str, dict] = {}
source: MqttClient | None = None


# ── WebSocket broadcast ──────────────────────────────────────────────────────

async def broadcast(msg: dict) -> None:
    if not ws_clients:
        return
    data = json.dumps(msg, ensure_ascii=False)
    dead: set[WebSocket] = set()
    for ws in ws_clients:
        try:
            await ws.send_text(data)
        except Exception:
            dead.add(ws)
    ws_clients.difference_update(dead)


async def _emit_event(ev: dict) -> None:
    await broadcast({"type": "event", "payload": ev})


async def _emit_gateway(gw_id: str, status: dict) -> None:
    await broadcast({"type": "gateway", "gwId": gw_id, "payload": status})


# ── Gateway reaper ──────────────────────────────────────────────────────────
# Gateway "online" is derived: any /event or /online tagged with gw="X" marks
# it online; this task flips it offline after GATEWAY_ONLINE_TTL_S of silence.

async def _gateway_reaper() -> None:
    while True:
        await asyncio.sleep(30.0)
        for gw_id in store.reap_gateways():
            _, gateways, _ = store.snapshot()
            entry = gateways.get(gw_id)
            if entry:
                await _emit_gateway(gw_id, entry)


# ── Mock source ──────────────────────────────────────────────────────────────
# Replays a virtual customer timeline against the v2 contract. The real gateway
# only emits /event on motion, so the mock does the same; there is no
# `vibration` or `ctr` field in v2.

_MOCK_GW = "seeedmote-gateway"
_MOCK_MACS = ["f0e3912cec19", "f0e3912cec20", "f0e3912cec21"]
_MOCK_TIMELINE: list[tuple[float, int]] = [
    (0.0,  0),
    (0.5,  0),
    (4.0,  0),
    (8.0,  1),
    (8.5,  1),
    (12.0, 2),
    (12.5, 2),
    (16.0, 1),
    (22.0, 2),
]
_MOCK_CYCLE = 30.0
_MOCK_RSSI = {0: -58, 1: -62, 2: -70}


async def _run_mock() -> None:
    pid_per_mac: dict[str, int] = {}

    gw_entry = store.touch_gateway(_MOCK_GW, rssi=-50)
    await _emit_gateway(_MOCK_GW, gw_entry)

    while True:
        cycle_start = asyncio.get_event_loop().time()
        for delay, mac_idx in _MOCK_TIMELINE:
            await asyncio.sleep(max(0.0, cycle_start + delay - asyncio.get_event_loop().time()))
            mac = _MOCK_MACS[mac_idx]
            pid_per_mac[mac] = (pid_per_mac.get(mac, 0) + 1) & 0xFF
            stored = store.add_event(
                mote_mac=mac,
                gw_id=_MOCK_GW,
                packet_id=pid_per_mac[mac],
                rssi=_MOCK_RSSI[mac_idx],
            )
            if stored:
                await _emit_event(stored)
                await _emit_gateway(_MOCK_GW, store.touch_gateway(_MOCK_GW, _MOCK_RSSI[mac_idx]))
        elapsed = asyncio.get_event_loop().time() - cycle_start
        await asyncio.sleep(max(0.0, _MOCK_CYCLE - elapsed))


# ── Lifespan ──────────────────────────────────────────────────────────────────

@asynccontextmanager
async def lifespan(app: FastAPI):
    global shoes, source

    with open(settings.shoes_yaml) as f:
        data = yaml.safe_load(f)
    shoes = data.get("shoes", {})
    logger.info("Loaded %d shoes from %s", len(shoes), settings.shoes_yaml)

    reaper = asyncio.create_task(_gateway_reaper())

    if settings.mock:
        logger.info("Running in MOCK mode")
        mock_task = asyncio.create_task(_run_mock())
    else:
        mock_task = None
        loop = asyncio.get_event_loop()
        source = MqttClient(
            store=store,
            broker=settings.mqtt_broker,
            port=settings.mqtt_port,
            user=settings.mqtt_user,
            password=settings.mqtt_password,
            loop=loop,
            on_event=_emit_event,
            on_gateway=_emit_gateway,
        )
        source.start()

    yield

    reaper.cancel()
    if mock_task is not None:
        mock_task.cancel()
    if source is not None:
        source.stop()


# ── App ───────────────────────────────────────────────────────────────────────

app = FastAPI(lifespan=lifespan)


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket) -> None:
    await ws.accept()
    ws_clients.add(ws)
    try:
        events, gateways, total = store.snapshot()
        await ws.send_text(json.dumps({
            "type": "snapshot",
            "events": events,
            "gateways": gateways,
            "total": total,
            "connected": source.is_connected() if source else settings.mock,
            "mock": settings.mock,
        }, ensure_ascii=False))
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        ws_clients.discard(ws)


class MqttConfigIn(BaseModel):
    broker: str
    port: int = 1883
    user: str | None = None
    password: str | None = None


@app.get("/api/config")
async def get_config() -> dict:
    return {
        "broker": settings.mqtt_broker,
        "port": settings.mqtt_port,
        "user": settings.mqtt_user,
        "mock": settings.mock,
    }


@app.post("/api/config")
async def update_config(cfg: MqttConfigIn) -> dict:
    global source
    if settings.mock:
        raise HTTPException(400, "Cannot change MQTT config in mock mode")

    if source is not None:
        source.stop()

    settings.mqtt_broker = cfg.broker
    settings.mqtt_port = cfg.port
    settings.mqtt_user = cfg.user
    if cfg.password is not None:
        settings.mqtt_password = cfg.password

    loop = asyncio.get_event_loop()
    source = MqttClient(
        store=store,
        broker=settings.mqtt_broker,
        port=settings.mqtt_port,
        user=settings.mqtt_user,
        password=settings.mqtt_password,
        loop=loop,
        on_event=_emit_event,
        on_gateway=_emit_gateway,
    )
    source.start()
    return {"ok": True}


@app.get("/api/shoes")
async def get_shoes() -> dict:
    return shoes


_assets_dir = Path(__file__).parent.parent / "assets"
if _assets_dir.exists():
    app.mount("/assets", StaticFiles(directory=_assets_dir), name="assets")

_frontend_dist = Path(__file__).parent.parent / "app" / "dist"
if _frontend_dist.exists():
    app.mount("/", StaticFiles(directory=_frontend_dist, html=True), name="frontend")


if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=settings.port, reload=False)
