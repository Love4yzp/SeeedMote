# app/ — Retail Interaction Demo

FastAPI + React 消费侧参考实现。订阅 SeeedMote v2 gateway 的事件型 MQTT 出口,
在 backend 投影成零售语义事件后,通过 WebSocket 推给 React 仪表盘。

```
┌─────────┐  BLE adv   ┌───────────┐  MQTT pub                ┌────────┐  WS push  ┌──────────────┐
│  Mote   │ ─────────▶ │  Gateway  │ ───────────────────────▶ │ Broker │ ────────▶ │  FastAPI     │ ──▶ React UI
│ (shoe)  │  BTHome    │ ESP32-S3  │  seeedmote/<mac>/event   └────────┘           └──────────────┘
└─────────┘            └───────────┘  seeedmote/<mac>/online
```

## MQTT 契约(权威:`AGENTS.md §5.2`)

| Topic | 触发 | Payload |
|---|---|---|
| `seeedmote/<mac>/event`  | mote 动作 | `{"packet_id": N, "rssi": -55, "gw": "<gw_name>"}` |
| `seeedmote/<mac>/online` | mote boot 心跳 | `{"rssi": -55, "gw": "<gw_name>"}` |

- `<mac>` 是 lowercase 无冒号 MAC。
- Backend 通过 topic 段抽 `mote_mac`,通过 payload `gw` 字段推 gateway 在线
  (boot/event 任意一帧到达 → gateway "online";沉默 `GATEWAY_ONLINE_TTL_S` 后翻 offline)。
- 消费侧按 `(mote_mac, packet_id)` 去重(packet_id 是 uint8 wrap counter)。
- React 默认不展示 raw `packet_id` / `rssi` / `gw`。Backend 会把 raw event 转为
  `InteractionEvent`(`商品被拿起`、商品信息、登记状态),技术字段只放在诊断信息里。

## Run

```bash
make dev               # FastAPI :3001 + Vite :5173,真 MQTT
make dev MOCK=true     # 同上,但用脚本化 mock 数据(无需硬件/broker)
```

Mock 模式由后端 `_run_mock` 协程驱动,**严格遵循 v2 契约**(只发 motion event,无
`vibration`/`ctr` 旧字段)— 这是 fork 之后写消费逻辑的最小可信样例。

环境变量:
- `SEEEDMOTE_BROKER` / `SEEEDMOTE_BROKER_PORT`
- `SEEEDMOTE_BROKER_USER` / `SEEEDMOTE_BROKER_PASS`
- `MOCK=true` 切换 mock

## Register a new mote

编辑根级 `shoes.yaml`。Key = `mote_mac`(lowercase 无冒号)。
未注册的 mote 会以 `(xxxx)` 占位卡片出现,不需要改固件。

## Files

| Path | Purpose |
|------|---------|
| `backend/main.py`        | FastAPI + WebSocket 广播 + mock 协程 + gateway reaper |
| `backend/mqtt_client.py` | paho-mqtt 订阅,topic → store,推回调 |
| `backend/semantic_events.py` | raw gateway event → retail `InteractionEvent` |
| `backend/store.py`       | dedup + 历史缓冲 + 派生 gateway 状态 |
| `backend/settings.py`    | Pydantic Settings(env 配置) |
| `app/src/`               | React + Vite 前端 |
| `app/src/store.ts`       | Zustand store(events, gateways, ws 状态) |
| `shoes.yaml`             | mote_mac → SKU metadata |
| `assets/`                | 占位 SVG 鞋图 |
