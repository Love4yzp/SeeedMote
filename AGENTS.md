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
| `ZEPHYR_BASE` 路径 | 根 `dev` CLI(从 `--ncs` 派生) |
| nRF 板名 / DTS overlay | `mote/app.overlay` + `mote/prj.conf` |
| BTHome 对象映射 | §5 内联表 + [BTHome spec](https://bthome.io/format/) |
| Gateway 固件源码 | `gateway/main/*.c` + `gateway/platformio.ini` |
| 项目当前状态 / TODO / "做到哪" | `README.md` + `git log` |

---

## §1 工具链(mote = west + NCS,gateway = PlatformIO + ESP-IDF)

**Mote** (`mote/`) = XIAO nRF52840 Sense,走 **west + NCS**。
**Gateway** (`gateway/`) = XIAO ESP32-S3,走 **PlatformIO + ESP-IDF**,C 固件。

| 对象 | 工具链 | 改什么 |
|---|---|---|
| Mote 固件 | west + NCS | `mote/src/*.c`、`mote/prj.conf`、`mote/app.overlay` |
| Gateway 固件 | PlatformIO + ESP-IDF | `gateway/main/*.c`、`gateway/platformio.ini`、`gateway/sdkconfig.defaults` |

> **历史**: Gateway 曾用 ESPHome YAML(`gateway/esphome.yaml` 仍保留为历史参考),PR #21 (2026-06-08) 替换为 PlatformIO + ESP-IDF C 固件,以获得 BLE 扫描、MQTT 路由、WiFi 配网的完全控制。

---

## §2 三个永远不要做的事

### 1. 不要新建顶级目录 / 切换工具链

架构级决策由人发起,不由 AI 自选。

### 2. 不要改 BTHome 对象映射而不同步 mote + gateway 代码

`gateway/main/ble_scanner.c` 里的 BTHome 解析和 `mote/` 广播的对象必须保持一致。改一个必须同时改另一个——先写提案给用户。

### 3. 不要在 gateway 引入 ESPHome / bthome_receiver

Gateway 已从 ESPHome 迁移到 PlatformIO + ESP-IDF C。不要开倒车。历史原因见 §5.4。

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

构建:`./dev mote build`

### 任务 B — 改 gateway 固件

**在** `gateway/main/` 范围:

| 改动 | 文件 |
|---|---|
| BLE 扫描 / BTHome 解析 | `ble_scanner.c/.h` |
| MQTT topic / payload | `mqtt_mgr.c/.h` |
| WiFi 配网 / AP 模式 | `wifi_mgr.c/.h` |
| Web 配置界面 | `web_server.c/.h` + `web_ui.html` |
| NVS 持久化配置 | `nvs_config.c/.h` |
| Gateway ID 生成 | `gw_id.c/.h` |
| LED 控制 / 闪灯模式 | `led.c/.h` |
| 串口 CLI 调试命令 | `cli.c/.h` |
| 新模块 | 新建 `.c/.h` + `main/CMakeLists.txt` 的 `SRCS` 追加 |
| PlatformIO 配置 | `gateway/platformio.ini` |
| ESP-IDF SDK 默认配置 | `gateway/sdkconfig.defaults` |
| 分区表 | `gateway/partitions.csv` |
| WiFi/MQTT 测试凭据 | `gateway/secrets.yaml`(当前测试阶段提交默认值;不要放真实生产凭据) |

构建:`./dev gateway build`
烧录:`./dev gateway flash`

---

## §4 构建 / 烧录入口(统一走 `./dev`)

```bash
./dev doctor                         # 检查工具链 / secrets / 串口 / UF2 卷

# Mote
./dev mote build                     # 编译 mote
./dev mote build --debug             # 编译 USB CDC debug 固件
./dev mote flash                     # 编译 + 拷贝 UF2 到 bootloader 卷
./dev mote log                       # 串口日志
./dev mote run --debug               # flash + log
./dev mote clean                     # 清理构建产物

./dev mote build --ncs v2.9.2
./dev mote flash --volume /Volumes/XIAO-SENSE
./dev mote log --port /dev/cu.usbmodem101

# Gateway
./dev gateway build                  # pio run
./dev gateway flash                  # pio run -t upload
./dev gateway log                    # pio device monitor
./dev gateway run                    # flash + log
./dev gateway clean                  # pio run -t clean

./dev gateway flash --port /dev/cu.usbserial-xxx
./dev gateway log --port /dev/cu.usbserial-xxx

# App demo
./dev app run
./dev app run --mock
```

---

## §5 v2 通信协议总览

### 5.1 BTHome 对象表(mote 广播的字段)

`mote/` 固件广播的 BTHome v2 Service Data 对象,供 `gateway/main/ble_scanner.c` 对照。Service Data UUID: `0xFCD2`(BTHome v2,unencrypted,trigger-based)。

| Object | object_id | Type | 说明 |
|--------|-----------|------|------|
| packet_id | 0x00 | uint8 | multi-gateway dedup key,暴露 MQTT |
| moving | 0x22 | uint8 (bool) | `1`=motion event / `0`=boot heartbeat,**gateway 用它分流 topic** |

> **历史**:`vibration` (0x2C) + `count` (0x3E) 在 v4 设计中删除(Stage 2 后 IMU 硬件已过滤 noise,PICKUP/MOVING 二分失意义;`count` 角色被 `packet_id` + consumer 端 dedup 接管)。gateway 解析器对这两个旧 object_id 保留 tolerate 路径(`ble_scanner.c` 走对象 id+长度循环,不依赖固定 offset),方便老固件过渡。

### 5.2 MQTT topic

| Topic | 触发 | Payload |
|---|---|---|
| `seeedmote/mote/<mac_no_colons>/event` | `moving=1` 的 BTHome 帧 | `{"packet_id": N, "rssi": -55, "gw": "<gw_id>"}` |
| `seeedmote/mote/<mac_no_colons>/seen` | `moving=0` 的 boot heartbeat 帧 | `{"rssi": -55, "gw": "<gw_id>", "reason": "boot"}` |
| `seeedmote/gateway/<gw_id>/status` | gateway 启动 / MQTT 重连 | `{"gw": "<gw_id>", "version": "2026.06.08"}` |
| `seeedmote/gateway/cmd` | app 发给 gateway 的运维命令 | `{"gw": "<gw_id>", "cmd": "locate"}` |

- `<mac_no_colons>` = 小写无冒号 MAC,e.g. `aabbccddeeff`
- `<gw_id>` = `seeedmote-gw-` + MAC 后 3 字节 hex(例如 `seeedmote-gw-a1b2c3`),由 `gw_id.c` 在 boot 时从 WiFi MAC 派生,是稳定 gateway ID
- MQTT username 也在 gateway 启动早期自动设为同一个 `<gw_id>`;不要在 `gateway/secrets.yaml` 里维护每台设备的 username
- **不暴露 `moving` 到 payload** —— gateway 已根据它分流 topic,consumer 无需再看
- `/seen` 不是强在线语义,只表示 gateway 最近看见了 mote 的 boot heartbeat
- **Mote 下行无 MQTT topic** —— Mote 配置走 Web BT 直连;`seeedmote/gateway/cmd` 只允许 Gateway 自身运维命令,当前仅 `locate`

### 5.3 Gateway 过滤策略

Gateway 只用 **Service Data UUID `0xFCD2`** 过滤识别 Mote;**不**检查 BLE Local Name。
- ESP-IDF BLE scan 用 passive scan,只读 ADV_IND 的 payload,不发 SCAN_REQ
- `ble_scanner.c` 解析器要求 BTHome `packet_id` (0x00) + `moving` (0x22) 都出现才发 MQTT,等价于"只接 Mote 形状的 BTHome 帧"
- **不依赖 MAC 地址** —— 同一 gateway 可同时听多个 mote

### 5.4 Gateway 不使用 ESPHome / bthome_receiver

历史上 Gateway 用 ESPHome YAML + `bthome_receiver` 外部组件。PR #21 迁移到 PlatformIO + ESP-IDF C,原因:
- `bthome_receiver` 按"per-device sensor mapping"(Home Assistant 模型)设计,要求每个 mote 绑定固定 MAC,跟 v2 多 mote 单 gateway 冲突
- ESPHome 不暴露 per-packet RSSI,路由表算法没数据可用
- ESPHome 不支持按 BTHome 对象值动态选 MQTT topic(`/event` vs `/seen` 分流)
- 需要 WiFi AP 配网 + Web 配置界面 + NVS 持久化,ESPHome 抽象层太厚

整个 BTHome 解析在 `gateway/main/ble_scanner.c`,是 gateway 的全部 BTHome 表面。

### 5.5 Gateway 架构概览

| 模块 | 文件 | 职责 |
|---|---|---|
| BLE Scanner | `ble_scanner.c` | Passive BLE scan,解析 BTHome v2 Service Data,过滤 UUID 0xFCD2 |
| MQTT Manager | `mqtt_mgr.c` | MQTT 连接管理,event/seen/status topic 发布,cmd 订阅 |
| WiFi Manager | `wifi_mgr.c` | AP+STA 双模,AP 用于首次配网,STA 连上游 WiFi |
| Web Server | `web_server.c` | HTTP 80 端口配置界面(WiFi SSID/密码 + MQTT broker) |
| NVS Config | `nvs_config.c` | WiFi / MQTT 凭据持久化到 NVS Flash |
| Gateway ID | `gw_id.c` | 从 WiFi MAC 派生稳定 gateway ID |
| LED | `led.c` | GPIO21 LED,AP 客户端连接闪灯 / locate 命令闪灯 |
| CLI | `cli.c` | 串口调试命令(`info`, `status`, `wifi_scan`, `nvs_show`, `locate`, `restart` 等) |

### 5.6 事件型原则

数据平面仍只在 IMU WAKE_UP 触发时发新 BTHome 帧。控制平面允许 mote boot 后发一帧 `moving=0` 作 heartbeat,以及 event/boot 后开 30s connectable window。

---

## §6 任务结束前 — 自检 checklist

1. **撞到了表里没有的坑?** → 在 §7 `实测踩坑沉淀` 追加一行
2. **本文档某条规则被这次任务改变了?** → propose 修订
3. **BTHome 对象改了但 mote/gateway 没同步?** → 拆两步任务
4. **Gateway 新模块加了但 `main/CMakeLists.txt` 没追加?** → 补上

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
| LSM6DSL Stage 2 自管 INT1 时没有事件 | `CONFIG_LSM6DSL_TRIGGER_GLOBAL_THREAD=y` 会让 Zephyr 驱动把 INT1 配成 data-ready 路由,和 WAKE_UP/INACTIVITY 抢同一根 `irq-gpios` | 删掉 trigger Kconfig,应用层用 `I2C_DT_SPEC_GET(IMU_NODE)` 直接写 WAKE_UP 寄存器,自己挂 `GPIO_DT_SPEC_GET(IMU_NODE, irq_gpios)` 回调 |
| `bt_set_name("SEEED-xxxxxx")` 编译/运行不生效 | Zephyr 默认设备名是静态 Kconfig 字符串 | `prj.conf` 加 `CONFIG_BT_DEVICE_NAME_DYNAMIC=y`;adv data 的 Complete Local Name 仍要用运行时 `bt_name` 更新 `data_len` |
| ESPHome 2026.5.1 / pioarduino 55.03.38-1 编译 gateway 时反复报 `tool-esptoolpy` 不是 Python project | pioarduino 的 `tool-esptoolpy` 包是 PlatformIO metadata 包,但 `penv_setup.py` 仍尝试 `uv pip install -e` 该目录 | 这是非致命 warning,build 仍可成功;删 `~/.platformio/packages/tool-esptoolpy` 会重下同样内容,不能修 |

新行格式:`坑 | 触发条件 | 修复`。**追加到表尾,不要重排或删行**。

---

## §8 永远不要问的问题(决策已锁死)

| 你可能想问的 | 写死的答案 |
|---|---|
| Gateway 应该用 ESPHome 还是 PIO + ESP-IDF? | **PIO + ESP-IDF C**。ESPHome 已弃用(PR #21) |
| 我能不能在 gateway 用 ESPHome YAML? | **不能**。`gateway/esphome.yaml` 是历史残留,不再编译 |
| Gateway 用 `bthome_receiver` 外部组件? | **不用**。原因见 §5.4 |
| 我能不能加 OTA / MCUBoot? | v2.0 显式放弃,后续由人决策 |
| 我能不能在 src/ 里直接 `#include <zephyr.h>`? | **不能**,Zephyr 3.7 用带前缀的头(`<zephyr/kernel.h>`) |
| NCS 需要在 project 目录里 bootstrap? | NCS 工作区在 `~/ncs/<version>/` 共享;`ZEPHYR_BASE` 在 `./dev` 里从 `--ncs` 派生 |
| Downlink 走 MQTT broker? | **Mote 配置不走 MQTT**。配置走 Web BT 直连;Gateway 只订阅 `seeedmote/gateway/cmd` 做自身运维命令,当前仅 `locate` 闪灯 |
| Gateway 加 BLE Client 做下行? | **不**。Gateway 是上行哑管道 |
| iOS Safari 上 Web BT 配置? | **不支持**。v2.0 demo + 技术员限定 Chrome / Android |
