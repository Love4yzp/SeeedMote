import asyncio
import json
import logging
from collections.abc import Callable, Coroutine
from typing import Any

import paho.mqtt.client as mqtt

from store import EventStore

logger = logging.getLogger(__name__)


class MqttClient:
    EVENT_TOPIC = "mote/v1/+/event"
    STATUS_TOPIC = "mote/v1/+/status"

    def __init__(
        self,
        store: EventStore,
        broker: str,
        port: int,
        user: str | None,
        password: str | None,
        loop: asyncio.AbstractEventLoop,
        on_event: Callable[[dict], Coroutine[Any, Any, None]],
        on_status: Callable[[str, dict], Coroutine[Any, Any, None]],
    ) -> None:
        self._store = store
        self._broker = broker
        self._port = port
        self._loop = loop
        self._on_event = on_event
        self._on_status = on_status
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
        client.subscribe([(self.EVENT_TOPIC, 1), (self.STATUS_TOPIC, 1)])
        logger.info("MQTT connected, subscribed to event + status topics")

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        self._connected = False
        logger.info("MQTT disconnected: %s", reason_code)

    def _on_message(self, client, userdata, msg: mqtt.MQTTMessage):
        try:
            data: dict = json.loads(msg.payload)
        except (json.JSONDecodeError, UnicodeDecodeError):
            return

        topic: str = msg.topic
        if topic.endswith("/event"):
            stored = self._store.add_event(data)
            if stored is not None:
                asyncio.run_coroutine_threadsafe(
                    self._on_event(stored), self._loop
                )
        elif topic.endswith("/status"):
            parts = topic.split("/")
            gw_id: str = data.get("gw_id") or (parts[2] if len(parts) > 2 else "unknown")
            stored = self._store.set_gateway_status(gw_id, data)
            asyncio.run_coroutine_threadsafe(
                self._on_status(gw_id, stored), self._loop
            )
