import unittest

from semantic_events import to_interaction_event
from store import EventStore


SHOES = {
    "f0e3912cec19": {
        "sku": "SH-001",
        "name": "Air Runner 春季款",
        "color": "白 / 橙",
        "price": 599,
        "image": "assets/sh001.svg",
    }
}

GATEWAY_ALIASES = {
    "seeedmote-gateway": "深圳办公室-入口网关",
    "seeedmote-gw-a1b2c3": "booth-01-left",
}


class SemanticEventTests(unittest.TestCase):
    def test_registered_raw_event_becomes_pickup_interaction(self):
        raw = {
            "mote_mac": "f0e3912cec19",
            "gw_id": "seeedmote-gw-a1b2c3",
            "rssi": -62,
            "packet_id": 122,
            "_received_at": 1710000000.0,
        }

        ev = to_interaction_event(raw, SHOES, GATEWAY_ALIASES)

        self.assertEqual(ev["action"], "picked_up")
        self.assertEqual(ev["action_label"], "商品被拿起")
        self.assertTrue(ev["registered"])
        self.assertEqual(ev["item_label"], "SH-001 Air Runner 春季款")
        self.assertEqual(ev["item"]["sku"], "SH-001")
        self.assertEqual(ev["item"]["name"], "Air Runner 春季款")
        self.assertEqual(ev["source"]["gw_id"], "seeedmote-gw-a1b2c3")
        self.assertEqual(ev["source"]["gw_alias"], "booth-01-left")
        self.assertEqual(ev["source"]["gw_label"], "booth-01-left")
        self.assertEqual(ev["source"]["packet_id"], 122)
        self.assertEqual(ev["source"]["rssi"], -62)

    def test_unknown_mote_gets_unregistered_business_label(self):
        raw = {
            "mote_mac": "aabbccddeeff",
            "gw_id": "seeedmote-gw-a1b2c3",
            "rssi": -70,
            "packet_id": 7,
            "_received_at": 1710000001.0,
        }

        ev = to_interaction_event(raw, SHOES)

        self.assertFalse(ev["registered"])
        self.assertIsNone(ev["item"])
        self.assertEqual(ev["item_label"], "未登记设备 ddeeff")
        self.assertEqual(ev["source"]["mote_mac"], "aabbccddeeff")
        self.assertEqual(ev["source"]["gw_id"], "seeedmote-gw-a1b2c3")
        self.assertIsNone(ev["source"]["gw_alias"])
        self.assertEqual(ev["source"]["gw_label"], "seeedmote-gw-a1b2c3")

    def test_gateway_alias_can_map_other_id(self):
        raw = {
            "mote_mac": "f0e3912cec19",
            "gw_id": "seeedmote-gateway",
            "rssi": -62,
            "packet_id": 123,
            "_received_at": 1710000002.0,
        }

        ev = to_interaction_event(raw, SHOES, GATEWAY_ALIASES)

        self.assertEqual(ev["source"]["gw_id"], "seeedmote-gateway")
        self.assertEqual(ev["source"]["gw_alias"], "深圳办公室-入口网关")
        self.assertEqual(ev["source"]["gw_label"], "深圳办公室-入口网关")

    def test_store_dedup_still_collapses_same_mote_packet_window(self):
        store = EventStore()

        first = store.add_event("f0e3912cec19", "seeedmote-gw-a1b2c3", 122, -62)
        duplicate = store.add_event("f0e3912cec19", "seeedmote-gw-a1b2c3", 122, -61)

        self.assertIsNotNone(first)
        self.assertIsNone(duplicate)
        events, _, total = store.snapshot()
        self.assertEqual(len(events), 1)
        self.assertEqual(total, 1)


if __name__ == "__main__":
    unittest.main()
