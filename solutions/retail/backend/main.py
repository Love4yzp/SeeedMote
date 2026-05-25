import asyncio
import json
import logging
import time
from contextlib import asynccontextmanager
from pathlib import Path

import uvicorn
import yaml
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles

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


async def _emit_status(gw_id: str, status: dict) -> None:
    await broadcast({"type": "status", "gwId": gw_id, "payload": status})


# ── Mock source ──────────────────────────────────────────────────────────────

_MOCK_GW = "441bf6804166"
_MOCK_MACS = ["f0e3912cec19", "f0e3912cec20", "f0e3912cec21"]

# Real mote frames always carry moving=True; put-back is inferred by consumers
# when no recent pickup pulse arrives.
# (delay_s, mac_index, moving, vibration)
_MOCK_TIMELINE: list[tuple[float, int, bool, bool]] = [
    (0.0,  0, True,  True),
    (0.5,  0, True,  False),
    (4.0,  0, True,  True),
    (8.0,  1, True,  True),
    (8.5,  1, True,  False),
    (12.0, 2, True,  True),
    (12.5, 2, True,  False),
    (16.0, 1, True,  True),
    (22.0, 2, True,  True),
]
_MOCK_CYCLE = 30.0


async def _run_mock() -> None:
    ctr_per_mac: dict[str, int] = {}
    pid_per_mac: dict[str, int] = {}

    # announce gateway online
    gw_status = {"online": True, "reason": "connect", "gw_id": _MOCK_GW, "ip": "127.0.0.1", "wifi_ssid": "mock"}
    stored_gw = store.set_gateway_status(_MOCK_GW, gw_status)
    await _emit_status(_MOCK_GW, stored_gw)

    while True:
        cycle_start = asyncio.get_event_loop().time()
        for delay, mac_idx, moving, vibration in _MOCK_TIMELINE:
            await asyncio.sleep(max(0.0, cycle_start + delay - asyncio.get_event_loop().time()))
            mac = _MOCK_MACS[mac_idx]
            ctr_per_mac[mac] = ctr_per_mac.get(mac, 0) + 1
            pid_per_mac[mac] = pid_per_mac.get(mac, 0) + 1
            raw = {
                "ts": int(time.time() * 1000),
                "gw_id": _MOCK_GW,
                "mote_mac": mac,
                "rssi": -65,
                "moving": moving,
                "vibration": vibration,
                "pid": pid_per_mac[mac],
                "ctr": ctr_per_mac[mac],
            }
            stored = store.add_event(raw)
            if stored:
                await _emit_event(stored)
        # wait for remainder of cycle
        elapsed = asyncio.get_event_loop().time() - cycle_start
        await asyncio.sleep(max(0.0, _MOCK_CYCLE - elapsed))


# ── Lifespan ──────────────────────────────────────────────────────────────────

@asynccontextmanager
async def lifespan(app: FastAPI):
    global shoes, source

    # Load shoe registry
    with open(settings.shoes_yaml) as f:
        data = yaml.safe_load(f)
    shoes = data.get("shoes", {})
    logger.info("Loaded %d shoes from %s", len(shoes), settings.shoes_yaml)

    if settings.mock:
        logger.info("Running in MOCK mode")
        asyncio.create_task(_run_mock())
    else:
        loop = asyncio.get_event_loop()
        source = MqttClient(
            store=store,
            broker=settings.mqtt_broker,
            port=settings.mqtt_port,
            user=settings.mqtt_user,
            password=settings.mqtt_password,
            loop=loop,
            on_event=_emit_event,
            on_status=_emit_status,
        )
        source.start()

    yield

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


@app.get("/api/shoes")
async def get_shoes() -> dict:
    return shoes


# Static assets (shoe SVGs etc.)
_assets_dir = Path(__file__).parent.parent / "assets"
if _assets_dir.exists():
    app.mount("/assets", StaticFiles(directory=_assets_dir), name="assets")

# Serve built frontend in production
_frontend_dist = Path(__file__).parent.parent / "app" / "dist"
if _frontend_dist.exists():
    app.mount("/", StaticFiles(directory=_frontend_dist, html=True), name="frontend")


if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=settings.port, reload=False)
