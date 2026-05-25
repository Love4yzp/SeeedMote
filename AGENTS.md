# SeeedMote v2 — AI Agent Operating Manual

**先读完整份再动手**。读不完就回答用户 "我先读完手册再开始"。

这份文档是**规则 + append-only 沉淀**,不是项目状态记录。
- 想知道项目"在做什么 / 做到哪了" → 看 `README.md` + `git log`
- 想知道某个具体版本 / 路径 / 字段 → **查源头**(下面 §0 列了所有源头)
- 想知道"什么不能做 / 怎么做" → 留在本文档

---

## §0 权威来源(本文档绝不复述这些)

凡是会随时间变化的事实(版本号、路径、字段、状态),都在代码里有唯一权威。
**不要在这份文档里 inline 这些值**,要用就去读源头:

| 你需要知道的 | 唯一权威 |
|---|---|
| NCS / Zephyr 版本 | `projects/mote_motion_nrf52840/west.yml`(manifest pin) |
| ESP-IDF / PIO platform 版本 | `projects/gateway_basic_esp32s3/platformio.ini`(`platform = espressif32@...`) |
| `ZEPHYR_BASE` 路径 | 根 `Makefile`(从 `NCS_VERSION` 派生) |
| nRF 板名 / DTS overlay | `projects/<name>/app.overlay` + `prj.conf` |
| ESP32 板名 / sdkconfig | `projects/<name>/platformio.ini` + `sdkconfig.defaults` |
| BLE 帧 / MQTT topic / payload | `contracts/*.yaml` |
| 项目当前状态 / TODO / "做到哪" | `README.md` + `git log` |

如果你在本文档里看到了一个具体版本号或路径常量,**那是 bug**,请按 §6 自检 #3 上报。

---

## §1 双轨工具链 —— 看 chip 后缀,不看 role 前缀

**这是最容易翻车的地方**。

Project 命名:`<role>_<function>_<chip>`。**编译看 `_<chip>` 后缀,不看 `<role>` 前缀**。

| Chip 后缀 | 工具链 | 入口 |
|---|---|---|
| `_nrf52840` / `_nrf52*` | **west + nRF Connect SDK (NCS)** | `make PROJECT=<name> build` |
| `_esp32s3` / `_esp32c6` / `_esp32*` | **PlatformIO + ESP-IDF** | `make PROJECT=<name> build` |
| 其他(`_rp2040` / `_stm32*` / ...) | **未定** | 问人,不要自己选 |

Role(mote / gateway / bridge / edge_ai / ...)只是项目描述。**它不决定工具链**。
- `mote_motion_nrf52840` 走 west(因为 chip 是 nrf52840)
- 未来若有 `mote_motion_esp32c6`,**它走 PIO**(虽然 role 还是 mote)
- 未来若有 `gateway_basic_nrf52840`,**它走 west**(虽然 role 是 gateway)

**为什么这样分轨**(已是定论,不要质疑):
- PIO 的 Zephyr framework 卡在 **2.7.1 (2021)**,bug 多、无 XIAO 板,实测不可行
- ESP-IDF 在 PIO 上是现代版本(看 `platformio.ini` 取最新数字),工作良好
- NCS 是 nRF 系列的官方现代 SDK,west 是它的官方入口
- **人定规则,AI 不选**

---

## §2 三个永远不要做的事

### 1. 不要在错误工具链上跑 project

**判断方式**:看 project 名末尾的芯片后缀(§1 表)。

错用工具链的报错形态:
- 在 nRF project 跑 `pio run` → "Unknown env" 或试图建 `.pio/` 污染目录
- 在 ESP32 project 跑 `west build` → "no west workspace" 或试图找 `west.yml`

看到这种报错**立刻停下**,你走错路了。

### 2. 不要跨 `projects/` 改文件

每个 project 是**孤岛**。
- ✅ 改 `projects/mote_motion_nrf52840/src/main.c`
- ❌ 同一 commit 既改 mote 又改 gateway —— 拆两个任务,人来协调跨设备契约

### 3. 不要新建 project 目录 / 切换工具链

`projects/` 下新增子目录是**架构级决策**,由人发起。看到任务"加个新方案",**先停下来问**:
- project 名字、目标 board、工具链 → 人定
- 工具链由 board 决定:nRF5x = west,ESP32 = PIO

---

## §3 三个常见任务模板

### 任务 A — 给 mote 加业务

**只在** `projects/mote_motion_nrf52840/` 范围:

| 改动 | 文件 |
|---|---|
| 新业务 .c/.h | `src/*.c` + `CMakeLists.txt` 的 `target_sources` 追加 |
| Zephyr 配置 | `prj.conf` 加 `CONFIG_XXX=y` |
| 板特化 DTS overlay | `app.overlay` |
| 加 NCS 模块 | `west.yml` 加 entry,然后 `west update` |

构建:`make PROJECT=mote_motion_nrf52840 build`

### 任务 B — 给 gateway 加业务

**只在** `projects/gateway_basic_esp32s3/` 范围:

| 改动 | 文件 |
|---|---|
| 新业务 .c | `src/*.c` + `src/CMakeLists.txt` 的 `SRCS` 追加 |
| 新 ESP-IDF 组件 | `src/CMakeLists.txt` 的 `REQUIRES` 追加 |
| 启用 BT/WiFi/lwIP / 调内存 | `sdkconfig.defaults` |
| 改 app 版本号 | `CMakeLists.txt`(项目根)的 `set(PROJECT_VER ...)` |

构建:`make PROJECT=gateway_basic_esp32s3 build`(或省略 `PROJECT=`,默认就是它)

### 任务 C — 改跨设备契约(BTHome 对象映射 / MQTT 出口 schema)

**这是跨设备改动,不要自己做**。

- contracts 当前 schema 见 `contracts/*.yaml`(状态、版本、字段都在文件头)
- 加字段 / 改 enum / 改字节序 / 改 topic 都要**先写提案给用户**
- 等用户确认后,**拆成三个 commit**:`contract:` schema → `mote:` 实现 → `gateway:` 实现
- 反向也成立:看到 mote / gateway 代码与 contract 不一致,**先停下来报告**,不要单边修改

---

## §4 构建 / 烧录 入口(统一走 Makefile)

```bash
make                                         # 默认 = gateway,flash + monitor
make build                                   # 仅编译(默认 project)
make flash                                   # 编译 + 烧录
make monitor                                 # 串口
make run                                     # flash + monitor
make clean
make erase                                   # ESP32 only

make PROJECT=mote_motion_nrf52840 flash      # 切到 mote(自动走 west + UF2)
make PROJECT=gateway_basic_esp32s3 PORT=/dev/cu.usbmodem101 flash
```

**绕过 Makefile 直接调 `west` / `pio`**:仅当 Makefile 不支持你要做的事(例如 SWD flash、`west sign`、`pio device list` 等)。常规 build/flash/monitor 一律走 `make`,这样 `ZEPHYR_BASE` / NCS 版本 / UF2 卷名等都从环境变量集中管理,**版本一升所有命令自动跟上**。

---

## §5 五条铁律(架构不变量)

1. `projects/` 平铺,每个 project 独立 — AI 改一个,不允许动其他
2. 工具链由 chip 后缀绑死(§1)— AI 不能自选
3. 无 `lib/` / `shared/` / `common/` 顶级目录 — **第三个 project 出现前允许重复**(Rule of Three),由人 promote
4. role 是 project 命名属性,不是目录层级
5. 跨设备契约只能通过 `contracts/` 协调 — AI 不能单边改字段

---

## §6 任务结束前 — 自检 checklist(每次必过)

完成任务、准备汇报前,过一遍这 5 条。任何一条命中,**先停下来处理**,再宣布完成。

1. **撞到了表里没有的坑?** → 在 §8 `实测踩坑沉淀` 追加一行(propose,不直接提交)
2. **本文档某条规则 / 状态被这次任务改变了?** → propose 修订(例如某个 "未做" 的事做完了、某个"决策已锁死"的事被人重新打开了)
3. **本文档说"不能做"但实测可以的?** → **报告人**,不要自己改规则
4. **我新建了 `lib/` / `shared/` / `common/` 顶级目录?** → 立刻退回(违反铁律 #3)
5. **我跨了 `projects/`?** → 拆任务,跨 project 改动必须分两次

---

## §7 永远不要问的问题(决策已锁死)

| 你可能想问的 | 写死的答案 |
|---|---|
| 我应该用 PIO 还是 west? | 看 chip 后缀(§1)。Role 不决定工具链 |
| Role 决定工具链吗? | **不**,chip 家族决定 |
| PIO + Zephyr 现在能用吗? | **不能**,Zephyr 2.7.1 (2021) + 多个 bug |
| 这两个 project 能不能共享 lib? | **不能**(铁律 #3,Rule of Three) |
| 我能不能加 OTA / MCUBoot? | v2.0 已显式放弃,后续由人决策 |
| ESP32-C6 能当 mote 吗? | 当前架构里 mote = nRF52840(因为 System OFF ~0.4µA),ESP32-C6 sleep 不够低 |
| 我能不能在 src/ 里直接 `#include <zephyr.h>`? | **不能**,Zephyr 3.7 用带前缀的头(`<zephyr/kernel.h>`) |
| 我能不能把 NCS 装到全局? | NCS 工作区在 `~/ncs/<version>/` 共享;`ZEPHYR_BASE` 在 Makefile 派生,无需在 project 目录 bootstrap |

**如果某条被人推翻了** → §6 自检 #3 处理:报告 + propose 删除/改写,不要自己解锁。

---

## §8 实测踩坑沉淀(append-only)

| 坑 | 触发条件 | 修复 |
|---|---|---|
| PIO + ESP-IDF configure 阶段 git_describe 失败 | 项目根 `CMakeLists.txt` 没 `set(PROJECT_VER ...)` | 显式写死 `PROJECT_VER` |
| PIO Zephyr `import yaml` 失败 | PIO 安装时没带 pyyaml | `uv tool install platformio --with pyyaml --with west` —— **但即使修了 Zephyr 还是 2.7.1,没用** |
| ESP-IDF 把 `src_dir = projects` 当单一组件 | 用 monorepo 单根 `platformio.ini` 跨 project | 行不通,每个 ESP-IDF project 必须自己一份 `platformio.ini` |
| 在 mote/zephyr/ 子目录放 prj.conf | PIO Zephyr 期望那里它自己管 | mote 走 west 后,`prj.conf` 在 project 根目录 |

新行格式:`坑 | 触发条件 | 修复`。**追加到表尾,不要重排或删行**(沉淀是 append-only)。

---

## §9 元规则

- 这份文档 ≠ 项目状态。状态去 `README.md` + `git log`。本文档只写**不变量、规则、踩坑**。
- 这份文档的版本号 / 路径 / 字段不能 inline,要查 §0 列的源头。
- 发现规则与代码现实矛盾,**先报告 + propose patch**,不要按"看起来正确"的方式工作。
- 文档迭代是协作的一部分,不是 AI 的责任。但 §6 / §8 是**有触发器**的 propose,不是可选项。
