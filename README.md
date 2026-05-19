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

骨架阶段 — hello blink + 编译验证。**无业务代码、无 BLE、无 MQTT、无 contracts**。

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

# Mote(首次需 ~4 GB NCS 下载)
cd projects/mote_motion_nrf52840
west init -l . && west update                                # 一次性
west build -b xiao_ble                                       # 编译
cp build/zephyr/zephyr.uf2 /Volumes/XIAOBOOT/                # 烧录 (双击 RESET 进 XIAOBOOT)
```

完整命令矩阵见 [`docs/build.md`](docs/build.md)。

## Repo layout

```
seeedmote-v2/
├── README.md                       ← 你在这
├── projects/                       ← 平铺,每个 project 独立可编
│   ├── mote_motion_nrf52840/       ← west + NCS,XIAO BLE Sense
│   │   ├── west.yml + CMakeLists.txt + prj.conf + src/
│   └── gateway_basic_esp32s3/      ← PIO + ESP-IDF,XIAO ESP32-S3
│       ├── platformio.ini + CMakeLists.txt + sdkconfig.defaults + src/
├── boards/                         ← 自定义 board JSON(目前空,等需要时填)
├── contracts/                      ← 跨设备契约(本骨架为空)
└── docs/
    ├── for-ai-agents.md            ← ⭐ AI Agent 操作手册(必读)
    ├── architecture.md             ← 设计总览
    └── build.md                    ← 构建命令清单
```

## 五条铁律

1. **`projects/` 平铺,每个 project 独立** — AI 改一个不允许动其他
2. **工具链由 project 命名前缀绑死** — mote_*_nrf52840=west,*_esp32*=pio
3. **无 `lib/` 共享层** — 第三个 project 出现前允许重复
4. **role 是 project 属性,不是目录** — 用命名前缀区分
5. **`contracts/` 当前为空** — 真业务跑通后再立

## For AI agents

入场第一件事:**读 [`docs/for-ai-agents.md`](docs/for-ai-agents.md)**。它写死"永远不要问的问题"和"常见任务怎么做"。

## 这个仓库不是什么

- 不是一个具体方案 —— 方案叫 `seeedmote_ble_node`,在 [`suharvest/app_collaboration`](https://github.com/suharvest/app_collaboration)
- 不是 v2.0 SeeedMote 迁移仓库 —— v2.0 留在 `~/Seeed/dev/embedded/` 归档
- 不是 Seeed 官方产品 —— 是 Seeed 方案商团队的内部参考骨架

## License

TBD.
