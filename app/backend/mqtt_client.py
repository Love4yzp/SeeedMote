"""MQTT subscriber for the v2 gateway contract.

Topics (gateway publishes, AGENTS.md §5.2):
  seeedmote/<mac_no_colons>/event   -- motion: {"packet_id": N, "rssi": -55, "gw": "<gw_id>"}
  seeedmote/<mac_no_colons>/online  -- boot heartbeat: {"rssi": -55, "gw": "<gw_id>"}

Gateway-level liveness is derived from these messages: whenever any frame
arrives tagged with `gw`, that gateway is marked online; a reaper task (run
from main.py) flips it offline after GATEWAY_ONLINE_TTL_S of silence.
"""

import asyncio
import json
import logging
from collections.abc import Callable, Coroutine
from typing import Any

import paho.mqtt.client as mqtt

from store import EventStore

logger = logging.getLogger(__name__)

EVENT_TOPIC = "seeedmote/+/event"
ONLINE_TOPIC = "seeedmote/+/online"


class MqttClient:
    def __init__(
        self,
        store: EventStore,
        broker: str,
        port: int,
        user: str | None,
        password: str | None,
        loop: asyncio.AbstractEventLoop,
        on_event: Callable[[dict], Coroutine[Any, Any, None]],
        on_gateway: Callable[[str, dict], Coroutine[Any, Any, None]],
        on_transport: Callable[[bool], Coroutine[Any, Any, None]] | None = None,
    ) -> None:
        self._store = store
        self._broker = broker
        self._port = port
        self._loop = loop
        self._on_event = on_event
        self._on_gateway = on_gateway
        self._on_transport = on_transport
        self._connected = False

        self._client = mqtt.Client(
            client_id="seeedmote-retail-py",
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        )
        if user:
            self._client.username_pw_set(user, password)
        self._client.reconnect_delay_set(min_delay=1, max_delay=10)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message

    def start(self) -> None:
        self._client.connect_async(self._broker, self._port)
        self._client.loop_start()
        logger.info("MQTT connecting to %s:%d", self._broker, self._port)

    def stop(self) -> None:
        self._client.loop_stop()
        self._client.disconnect()

    def is_connected(self) -> bool:
        return self._connected

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            logger.warning("MQTT connect failed: %s", reason_code)
            return
        self._connected = True
        client.subscribe([(EVENT_TOPIC, 1), (ONLINE_TOPIC, 1)])
        logger.info("MQTT connected, subscribed to event + online topics")
        self._notify_transport(True)

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        self._connected = False
        logger.info("MQTT disconnected: %s", reason_code)
        self._notify_transport(False)

    def _notify_transport(self, connected: bool) -> None:
        if self._on_transport is None:
            return
        asyncio.run_coroutine_threadsafe(self._on_transport(connected), self._loop)

    def _on_message(self, client, userdata, msg: mqtt.MQTTMessage):
        try:
            data: dict = json.loads(msg.payload)
        except (json.JSONDecodeError, UnicodeDecodeError):
            return

        # Topic shape: seeedmote/<mac>/event | seeedmote/<mac>/online
        parts = msg.topic.split("/")
        if len(parts) != 3:
            return
        mote_mac = parts[1].lower()
        kind = parts[2]

        gw_id = str(data.get("gw") or "unknown")
        rssi = int(data.get("rssi", 0))

        if kind == "event":
            packet_id = int(data.get("packet_id", 0))
            stored = self._store.add_event(mote_mac, gw_id, packet_id, rssi)
            if stored is not None:
                asyncio.run_coroutine_threadsafe(self._on_event(stored), self._loop)
                # add_event already touched the gateway; broadcast its status
                gw_entry = self._store.touch_gateway(gw_id, rssi)
                asyncio.run_coroutine_threadsafe(
                    self._on_gateway(gw_id, gw_entry), self._loop
                )
        elif kind == "online":
            gw_entry = self._store.touch_gateway(gw_id, rssi)
            asyncio.run_coroutine_threadsafe(
                self._on_gateway(gw_id, gw_entry), self._loop
            )
