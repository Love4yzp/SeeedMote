# SeeedMote v2 — AI Agent Operating Manual

**先读完整份再动手**。

这份文档是**规则 + append-only 沉淀**,不是项目状态记录。
- 想知道项目"在做什么 / 做到哪了" → 看 `README.md` + `git log`
- 想知道某个具体版本 / 路径 / 字段 → **查源头**(§0 列了所有源头)
- 想知道"什么不能做 / 怎么做" → 留在本文档

---

## §0 权威来源(本文档绝不复述这些)

| 你需要知道的 | 唯一权威 |
|---|---|
| NCS / Zephyr 版本 | `mote/west.yml`(manifest pin) |
| `ZEPHYR_BASE` 路径 | 根 `Makefile`(从 `NCS_VERSION` 派生) |
| nRF 板名 / DTS overlay | `mote/app.overlay` + `mote/prj.conf` |
| BTHome 对象映射 | §5 内联表 + [BTHome spec](https://bthome.io/format/) |
| ESPHome gateway 配置 | `gateway/esphome.yaml` |
| 项目当前状态 / TODO / "做到哪" | `README.md` + `git log` |

---

## §1 工具链(mote = west,gateway = ESPHome)

**Mote** (`mote/`) = XIAO nRF52840 Sense,走 **west + NCS**。
**Gateway** (`gateway/`) = XIAO ESP32-S3,走 **ESPHome YAML**,不写 C。

| 对象 | 工具链 | 改什么 |
|---|---|---|
| Mote 固件 | west + NCS | `mote/src/*.c`、`mote/prj.conf`、`mote/app.overlay` |
| Gateway 配置 | ESPHome CLI | `gateway/esphome.yaml`、`gateway/secrets.yaml` |

不再有 PIO / ESP-IDF C 固件。

---

## §2 三个永远不要做的事

### 1. 不要在 gateway/ 写 C 固件

Gateway 是 **ESPHome YAML**。看到任何想在 `gateway/` 里写 `.c`、`CMakeLists.txt`、`platformio.ini` 的冲动——**停下来**。修改 `gateway/esphome.yaml` 就是全部。

### 2. 不要新建顶级目录 / 切换工具链

架构级决策由人发起,不由 AI 自选。

### 3. 不要改 BTHome 对象映射而不同步 mote 代码

`gateway/esphome.yaml` 里的 BTHome sensor type 必须和 `mote/` 广播的对象保持一致。改一个必须同时改另一个——先写提案给用户。

---

## §3 常见任务模板

### 任务 A — 给 mote 加业务

**只在** `mote/` 范围:

| 改动 | 文件 |
|---|---|
| 新业务 .c/.h | `src/*.c` + `CMakeLists.txt` 的 `target_sources` 追加 |
| Zephyr 配置 | `prj.conf` 加 `CONFIG_XXX=y` |
| 板特化 DTS overlay | `app.overlay` |
| 加 NCS 模块 | `west.yml` 加 entry,然后 `west update` |

构建:`make build`

### 任务 B — 改 gateway 配置

**只改** `gateway/esphome.yaml`:

- 加 BTHome sensor → `bthome:` 块里追加 sensor/binary_sensor
- 改 WiFi/MQTT → `gateway/secrets.yaml`(本地,不提交)
- 加 ESPHome automation → `on_value:` / `on_state:` blocks

部署:`esphome run gateway/esphome.yaml`

---

## §4 构建 / 烧录入口(统一走 Makefile)

```bash
make build                   # 编译 mote
make flash                   # 编译 + 拷贝 UF2 到 bootloader 卷
make monitor                 # 串口日志
make run                     # flash + monitor
make clean                   # 清理构建产物

make NCS_VERSION=v2.9.2 build
make UF2_VOLUME=/Volumes/XIAO-SENSE flash
make PORT=/dev/cu.usbmodem101 monitor

# Gateway
esphome run gateway/esphome.yaml
```

---

## §5 v2 通信协议总览

### 5.1 BTHome 对象表(mote 广播的字段)

`mote/` 固件广播的 BTHome v2 Service Data 对象,供 `gateway/esphome.yaml` 对照。Service Data UUID: `0xFCD2`(BTHome v2,unencrypted,trigger-based)。

| Object | object_id | Type | 说明 |
|--------|-----------|------|------|
| packet_id | 0x00 | uint8 | multi-gateway dedup key,暴露 MQTT |
| moving | 0x22 | uint8 (bool) | `1`=motion event / `0`=boot heartbeat,**gateway 用它分流 topic** |

> **历史**:`vibration` (0x2C) + `count` (0x3E) 在 v4 设计中删除(Stage 2 后 IMU 硬件已过滤 noise,PICKUP/MOVING 二分失意义;`count` 角色被 `packet_id` + consumer 端 dedup 接管)。**Phase 1 mote 固件落地前**,gateway 解析器对这两个 object_id 保留 tolerate 路径(`gateway/esphome.yaml` lambda 走对象 id+长度循环,不依赖固定 offset)。

### 5.2 MQTT topic(gateway 上行)

| Topic | 触发 | Payload |
|---|---|---|
| `seeedmote/<mac_no_colons>/event` | `moving=1` 的 BTHome 帧 | `{"packet_id": N, "rssi": -55, "gw": "<gw_name>"}` |
| `seeedmote/<mac_no_colons>/online` | `moving=0` 的 boot heartbeat 帧 | `{"rssi": -55, "gw": "<gw_name>"}` |

- `<mac_no_colons>` = 小写无冒号 MAC,e.g. `aabbccddeeff`
- `<gw_name>` = ESPHome `esphome.name`(每个 gateway 自己的标识)
- **不暴露 `moving` 到 payload** —— gateway 已根据它分流 topic,consumer 无需再看
- **下行无 MQTT topic** —— 配置走 Web BT 直连

### 5.3 Gateway 过滤策略

Gateway 只用 **Service Data UUID `0xFCD2`** 过滤识别 Mote;**不**检查 BLE Local Name。
- ESPHome `esp32_ble_tracker` 用 passive scan(`active: false`),只读 ADV_IND 的 payload,不发 SCAN_REQ。若 mote 没把 Complete Local Name 塞进 ADV payload(early WIP / 老固件就是如此),`x.get_name()` 返回空,任何 name 过滤都会静默吃光所有帧
- Lambda parser 再要求 BTHome `packet_id` (0x00) + `moving` (0x22) 都出现才发 MQTT,等价于"只接 Mote 形状的 BTHome 帧"
- **不依赖 MAC 地址** —— 同一 gateway 可同时听多个 mote

### 5.4 Gateway 不使用 `bthome_receiver` 外部组件

历史上 `gateway/esphome.yaml` 试过 `bthome_receiver`(社区 external component)。**v4 P0 决定回归 raw `esp32_ble_tracker.on_ble_advertise` lambda 解析**,原因:
- `bthome_receiver` 按"per-device sensor mapping"(Home Assistant 模型)设计,要求每个 mote 绑定固定 MAC,跟 v2 多 mote 单 gateway 冲突
- 不暴露 per-packet RSSI,路由表算法没数据可用
- 不支持按 BTHome 对象值动态选 MQTT topic(`/event` vs `/online` 分流)

整个 BTHome 解析在 `gateway/esphome.yaml` 内联 lambda,约 40 行,是 gateway 的全部 BTHome 表面。

### 5.5 事件型原则

数据平面仍只在 IMU WAKE_UP 触发时发新 BTHome 帧。控制平面允许 mote boot 后发一帧 `moving=0` 作 heartbeat(Phase 2 加),以及 event/boot 后开 30s connectable window(Phase 3 加)。

---

## §6 任务结束前 — 自检 checklist

1. **撞到了表里没有的坑?** → 在 §7 `实测踩坑沉淀` 追加一行
2. **本文档某条规则被这次任务改变了?** → propose 修订
3. **我在 `gateway/` 写了 C 代码?** → 立刻退回(违反 §2 #1)
4. **BTHome 对象改了但 mote/gateway 没同步?** → 拆两步任务

---

## §7 实测踩坑沉淀(append-only)

| 坑 | 触发条件 | 修复 |
|---|---|---|
| PIO + ESP-IDF configure 阶段 git_describe 失败 | 项目根 `CMakeLists.txt` 没 `set(PROJECT_VER ...)` | 显式写死 `PROJECT_VER` |
| PIO Zephyr `import yaml` 失败 | PIO 安装时没带 pyyaml | `uv tool install platformio --with pyyaml --with west` —— **但即使修了 Zephyr 还是 2.7.1,没用** |
| ESP-IDF 把 `src_dir = projects` 当单一组件 | 用 monorepo 单根 `platformio.ini` 跨 project | 行不通,每个 ESP-IDF project 必须自己一份 `platformio.ini` |
| 在 mote/zephyr/ 子目录放 prj.conf | PIO Zephyr 期望那里它自己管 | mote 走 west 后,`prj.conf` 在 project 根目录 |
| 在 prj.conf 注释掉 USB Kconfig 仍编出 USB 固件 | `boards/seeed/xiao_ble/xiao_ble_nrf52840_sense_defconfig` 硬塞 `CONFIG_USB_DEVICE_STACK=y`,且 board DTS 把 chosen=usb_cdc_acm_uart | release 构建里必须显式 `CONFIG_USB_DEVICE_STACK=n` + `CONSOLE=n` + `UART_CONSOLE=n` + `SERIAL=n` + `LOG_BACKEND_UART=n`;debug 再用 overlay 一次性翻回 |
| `CONFIG_PM=y` 在 nRF52840 + NCS v2.9.2 被静默丢弃 | `PM depends on HAS_PM`,但 nRF52 SoC 树没 `select HAS_PM`(只 nRF54H 有);.config 既不为 y 也不为 n | 不写 `CONFIG_PM=y`(idle thread 默认 WFI 就够 System ON sleep);`CONFIG_PM_DEVICE=y` 仍独立有效 |

新行格式:`坑 | 触发条件 | 修复`。**追加到表尾,不要重排或删行**。

---

## §8 永远不要问的问题(决策已锁死)

| 你可能想问的 | 写死的答案 |
|---|---|
| Gateway 应该用 PIO + ESP-IDF 还是 ESPHome? | **ESPHome**。C 固件已删除 |
| PIO + ESP-IDF C gateway 还在吗? | **不在**。已 git rm,历史在 git log |
| 我能不能在 `gateway/` 里写 C? | **不能**。Gateway = ESPHome YAML |
| 我能不能加 OTA / MCUBoot? | v2.0 显式放弃,后续由人决策 |
| 我能不能在 src/ 里直接 `#include <zephyr.h>`? | **不能**,Zephyr 3.7 用带前缀的头(`<zephyr/kernel.h>`) |
| NCS 需要在 project 目录里 bootstrap? | NCS 工作区在 `~/ncs/<version>/` 共享;`ZEPHYR_BASE` 在 Makefile 派生 |
| Gateway 用 `bthome_receiver` 外部组件? | **不用**。v4 P0 决定回归 raw `on_ble_advertise`(理由见 §5.4) |
| Downlink 走 MQTT broker? | **不**。配置走 Web BT 直连;gateway 不订阅任何 topic |
| Gateway 加 BLE Client 做下行? | **不**。Gateway 哑管道,纯 YAML |
| iOS Safari 上 Web BT 配置? | **不支持**。v2.0 demo + 技术员限定 Chrome / Android |
