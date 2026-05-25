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

## §5 BTHome 对象表(mote 广播的字段)

`mote/` 固件当前广播的 BTHome v2 Service Data 对象,供 `gateway/esphome.yaml` 对照:

| Object | object_id | Type | ESPHome sensor type |
|--------|-----------|------|---------------------|
| packet_id | 0x00 | uint8 | (去重用,不暴露为 sensor) |
| moving | 0x22 | uint8 (bool) | `moving` (binary_sensor) |
| vibration | 0x2C | uint8 (bool) | `vibration` (binary_sensor) |
| count | 0x3E | uint32 | `count` (sensor) |

Service Data UUID: `0xFCD2` (BTHome v2,unencrypted,trigger-based)。

**事件型原则**:mote 触发才广播,`count` 每业务事件递增一次,**不是 heartbeat**。

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
