# SeeedMote v2

参考架构家族 — Seeed 方案商团队的 BLE 低功耗节点 + 网关聚合 + MQTT 出口模板。

```
 ┌──────────────────┐  BLE adv   ┌──────────────────┐   MQTT   ┌────────────┐
 │  Mote            │ ─────────▶ │  Gateway         │ ───────▶ │ Your stack │
 │  XIAO nRF52840   │  BTHome v2 │  XIAO ESP32-S3   │          │ HA/MQTT/…  │
 │  west + NCS      │            │  ESPHome         │          │            │
 └──────────────────┘            └──────────────────┘          └────────────┘
```

## Status

端到端骨架打通并已验证(2026-05):

- **Mote**: LSM6DS3TR-C WAKE_UP / INACTIVITY 硬件中断驱动。静默时不广播;boot 发一段 `moving=0` BTHome heartbeat,动作触发发 `moving=1` burst,随后打开 30s connectable 配置窗口。BTHome v2 Service Data UUID=`0xFCD2`,对象精简为 `packet_id / moving`。
- **Gateway**: ESPHome raw `esp32_ble_tracker.on_ble_advertise` passive scan,按 Service Data UUID `0xFCD2` 过滤,解析 `packet_id + moving`,发布 MQTT `/event` 或 `/online`。配置在 `gateway/esphome.yaml`,无需自研 C 固件。
- **架构原则**: **事件型**(event-driven),不是数据型 IoT。Mote 业务动作才发事件、gateway 中继、broker 不持久化、消费侧 `(mote_mac, packet_id)` 去重。控制平面允许 boot heartbeat 和 30s Web BT 配置窗口。
- **未做**: System OFF 低功耗、消息认证 / allowlist、battery 字段(留 v2)。

## Quick start

```bash
# Mote(需先安装外置 NCS,详见 docs/build.md)
./dev doctor                                  # 检查本机工具链和设备可见性
./dev mote build                              # 编译 UF2
./dev mote flash                              # 拷贝 UF2 到 /Volumes/XIAO-SENSE/
./dev mote log                                # 串口日志

# Gateway(需先安装 ESPHome CLI: pip install esphome)
cp gateway/secrets.yaml.example gateway/secrets.yaml   # 填入 WiFi/MQTT/MAC
./dev gateway run
```

## Repo layout

```
seeedmote-v2/
├── README.md                       ← 你在这
├── AGENTS.md                       ← ⭐ AI Agent 操作手册(必读)
├── CLAUDE.md                       ←   Claude Code 自动 import AGENTS.md
├── dev                             ← 开发入口 CLI(mote/gateway/app/doctor)
├── Makefile                        ← 兼容转发层,主入口仍是 ./dev
├── mote/                           ← XIAO nRF52840 Sense 固件(west + NCS)
│   ├── west.yml + CMakeLists.txt + prj.conf + app.overlay + src/
├── gateway/
│   ├── esphome.yaml                ← XIAO ESP32-S3 ESPHome 配置
│   └── secrets.yaml.example        ← WiFi/MQTT/MAC 配置模板
├── app/                            ← 零售演示 web UI + backend
├── tools/
│   └── web-bt/                     ← Chrome Web BT 现场配置工具(单页静态)
└── docs/
    ├── architecture.md             ← 设计总览
    └── build.md                    ← 构建命令清单
```

## 架构简则

1. **Mote 固件**: 只改 `mote/`，走 `./dev mote build / flash`
2. **Gateway 配置**: 只改 `gateway/esphome.yaml`，走 `./dev gateway run`
3. **BTHome 是契约**: 对象映射见 `AGENTS.md §5`，mote 和 gateway 必须同步
4. **事件型**: 无周期 telemetry，无 raw 数据流，boot heartbeat 仅用于上线/配置窗口发现

## For AI agents

入场第一件事:**读 [`AGENTS.md`](AGENTS.md)**(Claude Code 通过根 `CLAUDE.md` 自动 import)。

## 这个仓库不是什么

- 不是一个具体方案 —— 方案叫 `seeedmote_ble_node`,在 [`suharvest/app_collaboration`](https://github.com/suharvest/app_collaboration)
- 不是 v2.0 SeeedMote 迁移仓库 —— v2.0 留在 `~/Seeed/dev/embedded/` 归档
- 不是 Seeed 官方产品 —— 是 Seeed 方案商团队的内部参考骨架

## License

TBD.
