from collections.abc import Mapping
from typing import Any


def to_interaction_event(raw: Mapping[str, Any], shoes: Mapping[str, Mapping[str, Any]]) -> dict:
    """Project the raw gateway event into the retail demo business language."""
    mote_mac = str(raw.get("mote_mac") or "").lower()
    item = shoes.get(mote_mac)

    source = {
        "mote_mac": mote_mac,
        "packet_id": int(raw.get("packet_id", 0)),
        "rssi": int(raw.get("rssi", 0)),
        "gw_id": str(raw.get("gw_id") or "unknown"),
    }

    if item is None:
        registered = False
        item_payload = None
        item_label = f"未登记设备 {mote_mac[-6:] or 'unknown'}"
    else:
        registered = True
        item_payload = dict(item)
        sku = str(item_payload.get("sku") or "").strip()
        name = str(item_payload.get("name") or "").strip()
        item_label = " ".join(part for part in (sku, name) if part)

    return {
        "action": "picked_up",
        "action_label": "商品被拿起",
        "item_label": item_label,
        "registered": registered,
        "item": item_payload,
        "source": source,
        "_received_at": float(raw.get("_received_at", 0)),
    }


def to_interaction_events(raw_events: list[dict], shoes: Mapping[str, Mapping[str, Any]]) -> list[dict]:
    return [to_interaction_event(ev, shoes) for ev in raw_events]
