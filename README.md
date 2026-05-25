# SeeedMote v2

参考架构家族 — Seeed 方案商团队的 BLE 低功耗节点 + 多网关聚合 + MQTT 出口模板。

```
 ┌──────────────────┐  BLE adv   ┌──────────────────┐   MQTT   ┌────────────┐
 │  Mote            │ ─────────▶ │  Gateway         │ ───────▶ │ Your stack │
 │  XIAO nRF52840   │            │  XIAO ESP32-S3   │          │ HA/MQTT/…  │
 │  west + NCS      │            │  PIO + ESP-IDF   │          │            │
 └──────────────────┘            └──────────────────┘          └────────────┘
```

## Status

端到端骨架打通并已验证(2026-05):

- **Mote**:LSM6DS3TR-C data-ready interrupt 驱动采样,状态机分类事件(MOVING / PICK_UP)并广播 BTHome v2 Service Data(UUID=`0xFCD2`,对象:`packet id/moving/vibration/count`)。`count` 严格按业务事件递增(burst 重发复用同一 count),`packet_id` 用于 BLE 链路去重,无周期 heartbeat。USB CDC 日志(product=`seeedmote-motion`,VID=0x2886,PID=0x0045)。
- **Gateway**:NimBLE OBSERVER 持续扫描,解码 BTHome v2 motion profile,Wi-Fi STA 接外部 MQTT broker,事件型上行(`mote/v1/{gw_id}/event` + `/status` retained + LWT),下行命令 `/cmd` 接受 `{"cmd":"ping"\|"start_ble"\|"status"}` JSON。UART 同步输出 JSON 便于本地调试。
- **Contracts**:三份 v1 契约,事件型对齐:
  - [`contracts/airframe.yaml`](contracts/airframe.yaml) —— mote → gateway BLE 帧
  - [`contracts/mqtt-uplink.yaml`](contracts/mqtt-uplink.yaml) —— gateway → broker → consumer (event + status)
  - [`contracts/mqtt-downlink.yaml`](contracts/mqtt-downlink.yaml) —— consumer → broker → gateway (cmd)
- **架构原则**:**事件型**(event-driven),不是数据型 IoT。Mote 触发才广播、gateway 中继 + 去噪、broker 不持久化、消费侧 `(mote_mac, ctr)` 去重。**没有周期 telemetry、没有 raw 数据流**。
- **未做**:System OFF 低功耗、消息认证 / allowlist、battery 字段(留 v2)。

## ⚠️ 双轨工具链(看 chip 后缀,不看 role 前缀)

Project 命名:`<role>_<function>_<chip>`。**工具链由芯片家族决定**:

| Chip 后缀 | 工具链 | 构建命令 |
|---|---|---|
| `_nrf52*` | **west + NCS v2.9.2** | `cd projects/<name> && west build -b <board>` |
| `_esp32*` | **PlatformIO + ESP-IDF 5.2.2** | `pio run -d projects/<name>` |
| `_rp2040` / 其他 | 未定,问人 | — |

**Role(mote / gateway / ...) 是项目描述,不决定工具链**:
- `mote_motion_nrf52840` 走 west(因为 chip 是 nrf52840)
- 未来若有 `mote_motion_esp32c6`,**它走 PIO**(虽然 role 还是 mote)
- 未来若有 `gateway_basic_nrf52840`,**它走 west**(虽然 role 是 gateway)

**为什么双轨**:PIO 的 Zephyr 卡在 2.7.1(2021),实测不可用,nRF 系列必须 native NCS。详见 [`docs/architecture.md`](docs/architecture.md)。

**AI 不允许自选工具链** —— 看 chip 后缀绑死。

## Quick start

```bash
# Gateway(已实测通过)
pio run -d projects/gateway_basic_esp32s3                   # 编译
pio run -d projects/gateway_basic_esp32s3 -t upload         # 烧录

# Mote(需先安装外置 NCS,详见 docs/build.md)
cd projects/mote_motion_nrf52840
ZEPHYR_BASE=~/ncs/v2.9.2/zephyr west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build_uf2  # 编译 UF2
cp build_uf2/zephyr/zephyr.uf2 /Volumes/XIAO-SENSE/           # 烧录 (上电后 5 秒内出现 /Volumes/XIAO-SENSE/)
```

完整命令矩阵见 [`docs/build.md`](docs/build.md)。

## Repo layout

```
seeedmote-v2/
├── README.md                       ← 你在这
├── AGENTS.md                       ← ⭐ AI Agent 操作手册(必读)
├── CLAUDE.md                       ←   Claude Code 自动 import AGENTS.md
├── Makefile                        ← 统一编译/烧录入口(隐藏 west / pio 差异)
├── projects/                       ← 平铺,每个 project 独立可编
│   ├── mote_motion_nrf52840/       ← west + NCS,XIAO BLE Sense
│   │   ├── west.yml + CMakeLists.txt + prj.conf + src/
│   └── gateway_basic_esp32s3/      ← PIO + ESP-IDF,XIAO ESP32-S3
│       ├── platformio.ini + CMakeLists.txt + sdkconfig.defaults + src/
├── boards/                         ← 自定义 board JSON(目前空,等需要时填)
├── contracts/                      ← 跨设备契约(事件型)
│   ├── airframe.yaml               ← mote → gateway BLE 帧 v1
│   ├── mqtt-uplink.yaml            ← gateway → consumer (event/status) v1
│   └── mqtt-downlink.yaml          ← consumer → gateway (cmd JSON) v1
└── docs/
    ├── architecture.md             ← 设计总览
    └── build.md                    ← 构建命令清单
```

## 五条铁律

1. **`projects/` 平铺,每个 project 独立** — AI 改一个不允许动其他
2. **工具链由 project 命名前缀绑死** — mote_*_nrf52840=west,*_esp32*=pio
3. **无 `lib/` 共享层** — 第三个 project 出现前允许重复
4. **role 是 project 属性,不是目录** — 用命名前缀区分
5. **跨设备契约改动是人决策** — 只能通过 `contracts/` 协调,AI 不能自己改字段

## For AI agents

入场第一件事:**读 [`AGENTS.md`](AGENTS.md)**(Claude Code 通过根 `CLAUDE.md` 自动 import)。它写死"永远不要问的问题"和"常见任务怎么做"。

## 这个仓库不是什么

- 不是一个具体方案 —— 方案叫 `seeedmote_ble_node`,在 [`suharvest/app_collaboration`](https://github.com/suharvest/app_collaboration)
- 不是 v2.0 SeeedMote 迁移仓库 —— v2.0 留在 `~/Seeed/dev/embedded/` 归档
- 不是 Seeed 官方产品 —— 是 Seeed 方案商团队的内部参考骨架

## License

TBD.
