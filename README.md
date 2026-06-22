# SeeedMote v2

**Turn physical product interactions into real-time digital insights.**
**把实体商品互动变成实时数字洞察。**

SeeedMote is an ultra-low-power BLE sensor + multi-gateway system that detects when customers pick up products on display, and delivers real-time events to your existing stack via MQTT.

SeeedMote 是一套超低功耗 BLE 传感器 + 多网关系统。当顾客在展区拿起商品时，系统实时检测并通过 MQTT 将事件推送到你现有的技术栈。

```
  ┌────────────────────┐  BLE adv   ┌────────────────────┐  MQTT   ┌──────────────┐
  │  Mote              │ ─────────▶ │  Gateway           │ ──────▶ │  Your Stack  │
  │  XIAO nRF52840     │  BTHome v2 │  XIAO ESP32-S3     │         │  HA / MQTT / │
  │  coin-size sensor  │            │  ESP-IDF C firmware │         │  custom app  │
  └────────────────────┘            └────────────────────┘         └──────────────┘
         ~$5 BOM                        ~$8 BOM                      bring your own
```

---

## Why SeeedMote / 为什么选 SeeedMote

| | Camera + CV 摄像头方案 | RFID | **SeeedMote** |
|---|---|---|---|
| **Privacy 隐私** | Captures faces 采集人脸 | Tag-only | No camera, no identity 无摄像头、无身份采集 |
| **Cost per SKU 单品成本** | High (shared infra) | ~$0.10 tag, $500+ reader | **~$5 mote, ~$8 gateway** |
| **Install 安装** | Wiring, NVR, calibration | Antenna placement | **Peel & stick, zero wiring 贴上即用、零布线** |
| **Battery 电池** | N/A (wired) | N/A (passive) | **CR2032, months of standby 纽扣电池、数月待机** |
| **Real-time 实时性** | Seconds (processing) | Milliseconds | **Milliseconds 毫秒级** |
| **Coverage 覆盖** | Fixed FOV | ~1m range | **Multi-gateway, 10m+ radius 多网关、10m+ 半径** |

---

## Use Cases / 应用场景

### Retail Product Engagement / 零售商品互动分析

Attach a mote to each display product. When a customer picks it up, the motion event flows through nearby gateways to your dashboard in milliseconds.

在每件展示商品上贴一个 mote。当顾客拿起商品，运动事件在毫秒内经由附近网关到达你的仪表盘。

**What you learn / 你能获得：**
- Which products attract the most hands-on interest / 哪些商品最吸引顾客拿起
- Peak interaction hours and zones / 互动高峰时段和热力区域
- Dwell-to-pickup ratios across store layouts / 不同陈列方案的停留-拿起转化率

### Beyond Retail / 更多场景

The same event-driven BLE architecture adapts to: / 同一套事件驱动 BLE 架构可延伸至：

- **Asset tracking / 资产追踪** — tools, equipment, containers moved unexpectedly / 工具、设备、容器的异常移动告警
- **Exhibition & museum / 展会与博物馆** — exhibit interaction heatmaps / 展品互动热力图
- **Smart agriculture / 智慧农业** — livestock activity detection / 畜禽活动检测
- **Industrial safety / 工业安全** — emergency button, tamper detection / 急停按钮、防拆检测

---

## How It Works / 工作原理

**Event-driven, not data-streaming. 事件驱动，不是数据流。**

```
  Customer picks up shoe         Mote wakes from sleep          Gateway relays via MQTT
  顾客拿起鞋子            →      Mote 从休眠中唤醒       →      网关通过 MQTT 中继
                                 broadcasts BLE event            to your backend
                                 广播 BLE 事件                   送达你的后端

  ┌─────────┐  IMU interrupt  ┌─────────┐  BTHome v2   ┌─────────┐  WiFi/MQTT  ┌─────────┐
  │ Product │ ──────────────▶ │  Mote   │ ───────────▶ │ Gateway │ ──────────▶ │   App   │
  │ 商品    │    motion       │  BLE    │   passive    │ ESP32   │             │  后端   │
  └─────────┘                 └─────────┘   scan       └─────────┘             └─────────┘
```

- **Mote sleeps until motion** — IMU hardware interrupt, zero power when idle / Mote 静默至有动作——IMU 硬件中断，空闲零功耗
- **Gateway with built-in Web UI** — ESP-IDF C firmware with AP provisioning, HTTP config page, and CLI diagnostics; switch MQTT brokers on the fly without reflashing / 网关内置 Web 配置——ESP-IDF C 固件，AP 配网 + HTTP 配置页 + CLI 诊断，现场切换 MQTT broker 无需重刷
- **Multi-gateway dedup** — multiple gateways can hear the same mote; consumer deduplicates by `(mac, packet_id)` / 多网关去重——多个网关可同时收到同一 mote，消费侧按 `(mac, packet_id)` 去重
- **Open protocol** — BTHome v2 + standard MQTT; works with Home Assistant, custom backends, or any MQTT client / 开放协议——BTHome v2 + 标准 MQTT，兼容 Home Assistant、自有后端或任意 MQTT 客户端

---

## Live Demo / 实时演示

The repo includes a ready-to-run retail dashboard (`app/`):

本仓库自带一个开箱即用的零售仪表盘（`app/`）：

```bash
./dev app run --mock     # no hardware needed / 无需硬件
```

The dashboard shows real-time pickup events, product cards with SKU info, interaction counts, and gateway status — all driven by the same MQTT event stream your production system would consume.

仪表盘展示实时拿起事件、带 SKU 信息的商品卡片、互动次数统计和网关状态——驱动它的 MQTT 事件流与你的生产系统完全一致。

---

## Quick Start / 快速上手

### 1. Check your environment / 检查环境

```bash
./dev doctor
```

### 2. Flash mote firmware / 烧录 Mote 固件

```bash
./dev mote build          # compile UF2 / 编译 UF2
./dev mote flash          # copy to bootloader volume / 拷贝到 bootloader 卷
./dev mote log            # serial monitor / 串口日志
```

Requires nRF Connect SDK (NCS). See [docs/build.md](docs/build.md) for one-time setup.

需要 nRF Connect SDK (NCS)。一次性安装流程见 [docs/build.md](docs/build.md)。

### 3. Flash gateway / 烧录网关

```bash
./dev gateway flash       # PlatformIO build + upload / 编译 + 烧录
./dev gateway log         # serial monitor / 串口日志
```

Requires PlatformIO CLI (`pip install platformio`). On first boot the gateway starts in AP mode — connect to its hotspot, open the Web UI at `192.168.4.1`, and configure WiFi + MQTT broker. Settings persist in NVS across reboots.

需要 PlatformIO CLI（`pip install platformio`）。首次启动网关进入 AP 模式——连接其热点，在 `192.168.4.1` 打开 Web 配置页，设置 WiFi 和 MQTT broker。配置持久化到 NVS，重启不丢。

### 4. Verify end-to-end / 端到端验证

Subscribe to your MQTT broker and observe: / 订阅 MQTT broker 并观察：

```
seeedmote/mote/<mac>/event  {"packet_id":42,"rssi":-68,"gw":"seeedmote-gw-a1b2c3"}
seeedmote/mote/<mac>/seen   {"rssi":-74,"gw":"seeedmote-gw-a1b2c3","reason":"boot"}
```

---

## MQTT Topics / MQTT 主题

| Topic | Trigger / 触发 | Payload |
|---|---|---|
| `seeedmote/mote/<mac>/event` | Product picked up / 商品被拿起 | `{"packet_id", "rssi", "gw"}` |
| `seeedmote/mote/<mac>/seen` | Mote boot heartbeat / Mote 启动心跳 | `{"rssi", "gw", "reason"}` |
| `seeedmote/gateway/<gw_id>/status` | Gateway online / 网关上线 | `{"gw", "version"}` |
| `seeedmote/gateway/cmd` | Locate gateway (LED flash) / 定位网关（闪灯） | `{"gw", "cmd": "locate"}` |

`<mac>` = lowercase MAC without colons. `<gw_id>` = `seeedmote-gw-` + last 3 bytes of WiFi MAC (e.g. `seeedmote-gw-a1b2c3`).

---

## Repo Layout / 仓库结构

```
SeeedMote/
├── mote/              Sensor firmware (XIAO nRF52840, west + NCS)
│                      传感器固件
├── gateway/           Gateway firmware (XIAO ESP32-S3, PlatformIO + ESP-IDF)
│                      网关固件
├── app/               Retail demo dashboard (FastAPI + React)
│                      零售演示仪表盘
├── tools/web-bt/      Chrome Web Bluetooth field config tool
│                      Chrome Web BT 现场配置工具
├── firmware-release/  Release build scripts
│                      发布构建脚本
├── docs/              Build guide & architecture notes
│                      构建指南与架构文档
├── dev                Unified CLI entry point
│                      统一 CLI 入口
└── AGENTS.md          AI agent operating manual (for contributors)
                       AI Agent 操作手册（贡献者用）
```

---

## Hardware BOM / 硬件清单

| Component / 组件 | Part / 型号 | Role / 角色 | Unit Cost |
|---|---|---|---|
| Mote | [Seeed XIAO nRF52840 Sense](https://www.seeedstudio.com/XIAO-nRF52840-Sense-p-5253.html) | BLE sensor node / BLE 传感器节点 | ~$15 (dev board) |
| Gateway | [Seeed XIAO ESP32-S3](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html) | BLE→WiFi→MQTT bridge / BLE→WiFi→MQTT 桥接 | ~$8 (dev board) |
| Battery | CR2032 coin cell / 纽扣电池 | Mote power / Mote 供电 | ~$0.50 |
| Broker | Any MQTT broker (Mosquitto, EMQX, CloudMQTT...) | Event bus / 事件总线 | Free–hosted |

For volume production, the XIAO module BOM drops to ~$5 (mote) and ~$8 (gateway).

量产时，XIAO 模组 BOM 可降至约 $5（mote）和 $8（网关）。

---

## Integration / 集成方式

SeeedMote speaks standard MQTT. Integrate with: / SeeedMote 使用标准 MQTT，可集成：

- **Home Assistant** — subscribe to MQTT topics, use HA automations / 订阅 MQTT topic，使用 HA 自动化
- **Node-RED / n8n** — subscribe to MQTT topics, build automation flows / 订阅 MQTT topic，构建自动化流程
- **Custom backend** — subscribe, dedup by `(mac, packet_id)`, build your business logic / 订阅、去重、构建你的业务逻辑
- **Cloud platforms** — bridge MQTT to AWS IoT Core, Azure IoT Hub, GCP IoT, or any cloud / 桥接 MQTT 到 AWS、Azure、GCP 或任意云平台

---

## Architecture Principles / 架构原则

| | Traditional IoT 传统 IoT | **SeeedMote** |
|---|---|---|
| **Uplink** | Periodic sampling 周期采样 | Event-triggered only 仅事件触发 |
| **Silence** | Anomaly ("data lost") 异常 | Normal ("nothing happened") 正常 |
| **Gateway** | Data aggregator 数据汇聚器 | Stateless relay 无状态中继 |
| **Broker** | Time-series store 时序存储 | Event bus 事件总线 |

---

## Gateway Features / 网关功能

The gateway runs a custom ESP-IDF C firmware with: / 网关运行自研 ESP-IDF C 固件，具备：

- **AP provisioning** — starts as WiFi hotspot on first boot for zero-touch setup / AP 配网——首次启动自动开热点，零接触配置
- **Web config UI** — browser-based WiFi and MQTT broker configuration at `http://<gateway-ip>` / Web 配置界面——浏览器访问即可配置 WiFi 和 MQTT broker
- **CLI diagnostics** — serial commands (`info`, `status`, `wifi_scan`, `nvs_show`, `locate`, `restart`) / CLI 诊断——串口命令支持状态查询、WiFi 扫描、NVS 查看等
- **NVS persistence** — WiFi and MQTT settings survive power cycles / NVS 持久化——WiFi 和 MQTT 配置断电不丢
- **Locate command** — flash LED via MQTT to find a gateway in the field / 定位命令——通过 MQTT 远程闪灯定位网关

---

## Status / 项目状态

End-to-end skeleton validated (2026-06): / 端到端骨架已验证（2026-06）：

- Mote: LSM6DS3TR-C hardware interrupt (WAKE_UP / INACTIVITY), BTHome v2 advertising, 30s Web BT config window after event
- Gateway: ESP-IDF passive BLE scan → MQTT, AP provisioning + Web config UI + CLI, unified firmware with auto MAC-derived ID
- App: FastAPI + React real-time dashboard with mock mode
- Field config: Chrome Web Bluetooth tool for on-site mote provisioning

**Roadmap / 路线图：** System OFF deep sleep, message authentication, battery reporting, allowlist.

---

## For AI Agents / AI 贡献者

Read [`AGENTS.md`](AGENTS.md) first (auto-imported by Claude Code via `CLAUDE.md`).

入场第一件事：读 [`AGENTS.md`](AGENTS.md)。

---

## License

TBD.
