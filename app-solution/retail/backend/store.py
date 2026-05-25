import threading
import time
from collections import deque


class EventStore:
    def __init__(self, max_events: int = 500) -> None:
        self._events: deque[dict] = deque(maxlen=max_events)
        self._seen: set[str] = set()
        self._last_ctr: dict[str, int] = {}
        self._gw_status: dict[str, dict] = {}
        self._total: int = 0
        self._lock = threading.Lock()

    def add_event(self, raw: dict) -> dict | None:
        mac = raw.get("mote_mac")
        if not mac:
            return None
        ctr = int(raw.get("ctr", 0))
        with self._lock:
            last = self._last_ctr.get(mac)
            if last is not None and ctr < last:
                # mote rebooted — clear dedup keys for this mac
                self._seen = {k for k in self._seen if not k.startswith(f"{mac}:")}
            self._last_ctr[mac] = ctr
            key = f"{mac}:{ctr}"
            if key in self._seen:
                return None
            self._seen.add(key)
            stored = {
                **raw,
                "moving": bool(raw.get("moving", False)),
                "vibration": bool(raw.get("vibration", False)),
                "_received_at": time.time(),
            }
            self._events.appendleft(stored)
            self._total += 1
            return stored

    def set_gateway_status(self, gw_id: str, raw: dict) -> dict:
        stored = {**raw, "_received_at": time.time()}
        with self._lock:
            self._gw_status[gw_id] = stored
        return stored

    def snapshot(self) -> tuple[list[dict], dict[str, dict], int]:
        with self._lock:
            return list(self._events), dict(self._gw_status), self._total
