# solutions/retail — Retail Interaction Demo

React + Node.js consumer for SeeedMote v2 event-driven uplink. Node backend subscribes to `mote/v1/+/event` / `mote/v1/+/status` and streams events over WebSocket to a React dashboard showing live retail floor state.

```
┌─────────┐  BLE adv  ┌───────────┐  MQTT pub  ┌────────┐  WS push  ┌──────────────┐
│  Mote   │──────────▶│  Gateway  │───────────▶│ Broker │──────────▶│ Node server  │──▶ React UI
│ (shoe)  │  BTHome   │ ESP32-S3  │  mote/v1/…          └────────┘           └──────────────┘
└─────────┘           └───────────┘
```

## Run

### Mock mode (no hardware, no broker)

```bash
cd solutions/retail
npm install
npm run dev
```

Open http://localhost:5173. A scripted virtual-customer timeline replays three shoes being picked up and handled; put-back state is inferred from no recent pickup events.

By default `npm run dev` starts both the Node server (port 3001) and the Vite dev server (port 5173).

To run the server in mock mode:
```bash
# In one terminal:
cd solutions/retail && node --loader tsx server/src/index.ts --mock

# In another:
cd solutions/retail/app && npm run dev
```

### Live mode (real gateway + broker)

```bash
# defaults to localhost:1883
SEEEDMOTE_BROKER=192.168.1.100 npm run dev:live

# with auth
SEEEDMOTE_BROKER=192.168.1.100 SEEEDMOTE_BROKER_USER=user SEEEDMOTE_BROKER_PASS=pass npm run dev:live
```

## Register a new mote

Edit `shoes.yaml` at the retail root. Keys are the lowercase MAC published as `mote_mac`.

## Files

| Path | Purpose |
|------|---------|
| `server/src/index.ts` | Express + WebSocket server; routes MQTT events to WS clients |
| `server/src/mqttClient.ts` | MQTT.js subscriber; publishes to EventStore |
| `server/src/mockSource.ts` | Scripted customer timeline; same interface as MqttClient |
| `server/src/eventStore.ts` | Dedup + history buffer |
| `app/src/` | React + Vite frontend |
| `app/src/store.ts` | Zustand state — events, gateways, connection state |
| `shoes.yaml` | mote_mac → SKU metadata |
| `assets/` | Placeholder SVG shoe images |
