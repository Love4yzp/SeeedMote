import threading
import time
from collections import deque


# Gateway is "online" if any /event or /seen from it arrived within this many
# seconds. Mote firmware emits a /seen heartbeat at boot, and /event on every
# motion, so any active mote keeps its gateway marked online.
GATEWAY_ONLINE_TTL_S = 120.0

# packet_id is a uint8 wrapping counter. Multi-gateway dedup only needs to
# squash the race window where 2 gateways forward the same BLE adv (sub-second).
# Outside that window, the same (mac, pid) is a legitimate new event (wrap or
# reboot). Keep dedup keys with a TTL.
DEDUP_TTL_S = 2.0


class EventStore:
    """In-memory event buffer for the v2 gateway contract.

    Schema (mirrors AGENTS.md §5.2):
      MotionEvent = {mote_mac, gw_id, rssi, packet_id, _received_at}
      GatewayStatus = {gw_id, online, last_seen, version?}
    """

    def __init__(self, max_events: int = 500) -> None:
        self._events: deque[dict] = deque(maxlen=max_events)
        self._seen: dict[tuple[str, int], float] = {}
        self._gw: dict[str, dict] = {}
        self._total: int = 0
        self._lock = threading.Lock()

    def add_event(self, mote_mac: str, gw_id: str, packet_id: int, rssi: int) -> dict | None:
        if not mote_mac:
            return None
        now = time.time()
        with self._lock:
            key = (mote_mac, packet_id)
            prev = self._seen.get(key)
            if prev is not None and now - prev < DEDUP_TTL_S:
                return None
            self._seen[key] = now
            # opportunistic GC of stale dedup entries
            if len(self._seen) > 4096:
                self._seen = {k: t for k, t in self._seen.items() if now - t < DEDUP_TTL_S}

            stored = {
                "mote_mac": mote_mac,
                "gw_id": gw_id,
                "rssi": rssi,
                "packet_id": packet_id,
                "_received_at": now,
            }
            self._events.appendleft(stored)
            self._total += 1
            self._touch_gateway_locked(gw_id, rssi)
            return stored

    def touch_gateway(
        self,
        gw_id: str,
        rssi: int | None = None,
        *,
        version: str | None = None,
    ) -> dict:
        with self._lock:
            return self._touch_gateway_locked(gw_id, rssi, version=version)

    def _touch_gateway_locked(
        self,
        gw_id: str,
        rssi: int | None,
        *,
        version: str | None = None,
    ) -> dict:
        now = time.time()
        entry = self._gw.get(gw_id, {"gw_id": gw_id, "last_rssi": None})
        entry["last_seen"] = now
        entry["online"] = True
        if rssi is not None:
            entry["last_rssi"] = rssi
        if version is not None:
            entry["version"] = version
        self._gw[gw_id] = entry
        return entry

    def reap_gateways(self) -> list[str]:
        """Mark stale gateways offline. Returns list of gw_ids that flipped."""
        now = time.time()
        flipped: list[str] = []
        with self._lock:
            for gw_id, entry in self._gw.items():
                stale = now - entry.get("last_seen", 0) > GATEWAY_ONLINE_TTL_S
                if stale and entry.get("online", True):
                    entry["online"] = False
                    flipped.append(gw_id)
        return flipped

    def snapshot(self) -> tuple[list[dict], dict[str, dict], int]:
        with self._lock:
            return list(self._events), dict(self._gw), self._total
