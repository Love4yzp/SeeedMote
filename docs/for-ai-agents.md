# For AI Agents — SeeedMote v2 操作手册

**你必须先读完整份再动手**。读不完就回答用户"我先读完手册再开始"。

---

## 你在哪

这是 **SeeedMote v2** 仓库 —— Seeed 方案商团队的"参考架构家族"骨架。它衍生具体方案(冷链、急停、接触、资产追踪 …),最终通过 [SenseCraft Solution 平台](https://github.com/suharvest/app_collaboration) 投给集成商。

当前状态:**端到端骨架打通**。

- mote 走到 bring-up step 2:BLE adv 静态 SeeedMote 帧
- gateway 实现 BLE observer + UART JSON 出口
- 跨设备契约:`contracts/airframe.yaml` v1(11 字节,uplink only,无 MIC)
- 仍未做:MQTT 出口、IMU 触发、System OFF、消息认证

---

## ⚠️ 双轨工具链 —— 这是你最容易翻车的地方

**工具链由芯片家族决定,不由 role 决定**。

Role 和 chip 是**两个独立维度**:
- **Role**(职能)= `mote` / `gateway` / 未来的 `bridge` / `edge_ai` 等 —— 这个 project 做什么
- **Chip**(芯片家族)= `nrf52840` / `esp32s3` / `esp32c6` / 未来的 `rp2040` 等 —— 跑在什么硬件上

project 名格式:`<role>_<function>_<chip>`。**编译看 `_<chip>` 后缀,不看 `<role>` 前缀**。

| Chip 后缀 | 工具链 | 入口命令 |
|---|---|---|
| `_nrf52840` / `_nrf52*` | **west + nRF Connect SDK (NCS v2.9.2)** | `cd projects/<name> && west build -b <zephyr_board>` |
| `_esp32s3` / `_esp32c6` / `_esp32*` | **PlatformIO + ESP-IDF** | `pio run -d projects/<name>` |
| 其他(`_rp2040` / `_stm32*` / ...) | **未定** | 问人,不要自己选 |

**这意味着**:
- `mote_motion_nrf52840` 走 west(因为芯片是 nrf52840)
- `gateway_basic_esp32s3` 走 PIO(因为芯片是 esp32s3)
- 如果未来出现 `mote_motion_esp32c6`(把 mote 建在 ESP32-C6 上),**它走 PIO**,不是 west
- 如果未来出现 `gateway_basic_nrf52`(把 gateway 建在 nRF 上),**它走 west**,不是 PIO

**Role 只是命名描述**,告诉读者 project 是干啥的(电池端 / 网关端 / 桥接 / 等)。**它不决定工具链**。

**为什么这样分轨**(已是定论,不要质疑):
- PIO 的 Zephyr framework 卡在 **2.7.1(2021)**,bug 多、无 XIAO 板,**实测不可行**
- ESP-IDF 在 PIO 上是 **5.2.2 现代版本**,工作良好
- NCS 是 nRF 系列的官方现代 SDK,west 是它的官方入口
- 用 native SDK 给每边最现代的能力,**人定规则,AI 不选**

---

## 三个永远不要做的事

### 1. 不要在错误工具链上跑 project

判断方式:**看 project 名末尾的芯片后缀**。

| 后缀 | 用 | 不能用 |
|---|---|---|
| `_nrf52840` / `_nrf52*` | `west build` / `west flash` | `pio run` 任何形式 |
| `_esp32s3` / `_esp32c6` / `_esp32*` | `pio run -d projects/<name>` | `west build` |

错用工具链的报错形态:
- 在 nRF project 跑 `pio run` → "Unknown env" 或试图建 `.pio/` 污染目录
- 在 ESP32 project 跑 `west build` → "no west workspace" 或试图找 `west.yml`

看到这种报错**立刻停下**,你走错路了。

### 2. 不要跨 `projects/` 改文件

每个 project 是**孤岛**。
- ✅ 改 `projects/mote_motion_nrf52840/src/main.c` —— OK
- ❌ 同一个 commit 既改 mote 又改 gateway —— 拆两个任务,人来协调跨设备契约

### 3. 不要新建 project 目录 / 切换工具链

`projects/` 下新增子目录是**架构级决策**,由人发起。看到任务"加个新方案",**先停下来问人**:
- project 名字、目标 board、工具链 → 人定
- 工具链由 board 决定:nRF5x = west,ESP32 = PIO

---

## 三个常见任务模板

### 任务 A — 给 mote 加业务(传感器 / BLE 字段 / 行为)

**只在** `projects/mote_motion_nrf52840/` 范围:

| 改动 | 文件 |
|---|---|
| 新业务 .c/.h | `src/*.c` + `CMakeLists.txt` 的 `target_sources` 追加 |
| Zephyr 配置 | `prj.conf` 加 `CONFIG_XXX=y` |
| 板特化 DTS overlay | `app.overlay`(覆盖 board DTS) |
| 加 NCS 模块 | `west.yml` 加 project entry,然后 `west update` |

构建:`cd projects/mote_motion_nrf52840 && west build -b xiao_ble`

### 任务 B — 给 gateway 加业务(出口适配 / 扫描 / 配置)

**只在** `projects/gateway_basic_esp32s3/` 范围:

| 改动 | 文件 |
|---|---|
| 新业务 .c | `src/*.c` + `src/CMakeLists.txt` 的 `SRCS` 追加 |
| 新 ESP-IDF 组件 | `src/CMakeLists.txt` 的 `REQUIRES` 追加 |
| 启用 BT/WiFi/lwIP | `sdkconfig.defaults` |
| 改 app 版本号 | `CMakeLists.txt`(项目根)的 `set(PROJECT_VER ...)` |

构建:`pio run -d projects/gateway_basic_esp32s3`

### 任务 C — 改跨设备契约(BLE 帧格式 / 出口 JSON 字段)

**这是跨设备改动,不要自己做**。

- 当前 contract: [`contracts/airframe.yaml`](../contracts/airframe.yaml) v1(mote → gateway,11 字节,uplink only)
- 加字段 / 改 enum / 改字节序 都要先写提案给用户
- 等用户确认后,**拆成三个 commit**:`contract:` schema → `mote:` 实现 → `gateway:` 实现
- 反向也成立:看到 mote / gateway 代码与 contract 不一致,**先停下来报告**,不要单边修改

---

## 构建 / 烧录 命令速查(死记硬背)

```bash
# ━━━ MOTE (west + NCS) ━━━
cd projects/mote_motion_nrf52840
west init -l .                     # 一次性,建 .west/
west update                        # 一次性,下载 NCS 4GB
west build -b xiao_ble             # 编译
cp build/zephyr/zephyr.uf2 /Volumes/XIAOBOOT/   # 烧录 (UF2)
# 或:
west flash                         # 烧录 (SWD)

# ━━━ GATEWAY (PIO + ESP-IDF) ━━━
pio run -d projects/gateway_basic_esp32s3                  # 编译
pio run -d projects/gateway_basic_esp32s3 -t upload        # 烧录
pio device monitor -d projects/gateway_basic_esp32s3       # 串口监视
```

**绝对不会改的事**:
- ❌ 把 `pio` 命令用在 mote
- ❌ 把 `west` 命令用在 gateway
- ❌ 自己装 NCS / ESP-IDF —— 都通过工具链各自的安装流程

---

## 永远不要问的问题(决策已锁死)

| 你可能想问的 | 写死的答案 |
|---|---|
| 我应该用 PIO 还是 west? | **看 project 名末尾的芯片后缀**:`_nrf52*` 用 west,`_esp32*` 用 pio。**Role(mote/gateway)与工具链无关** |
| Role 决定工具链吗? | **不**,芯片家族决定。mote on ESP32 走 PIO,gateway on nRF52 走 west(虽然这两种组合目前没建) |
| PIO + Zephyr 现在能用吗? | **不能**。Zephyr 2.7.1(2021)+ 多个 bug,实测不可行 |
| 这两个 project 能不能共享 lib? | **不能**,前两个 project 允许重复,第三个出现才考虑抽 |
| 我能不能加 OTA / MCUBoot? | **不能**,v2.0 已显式放弃,v2.1 由人决策 |
| ESP32-C6 能当 mote 吗? | 当前架构里 mote = nRF52840(因为 System OFF ~0.4µA),ESP32-C6 sleep 不够低 |
| 我能不能在 src/ 里直接 #include <zephyr.h>?(老 API) | **不能**,我们用 Zephyr 3.7,头文件是 `<zephyr/kernel.h>` 这种带前缀的 |
| 我能不能把 NCS 装到全局? | **不能**,每个 mote project 自己的 west workspace |

---

## 必须问的问题(任务边界模糊时)

| 何时问 | 怎么问 |
|---|---|
| 任务跨 project | "这个改动涉及 mote 和 gateway,我先做哪一个?另一个独立任务行吗?" |
| 任务涉及 contracts/ | "contracts/ 里目前没有相应 schema,要先立吗?字段怎么定?" |
| 任务要新建 project | "新建 project,我建议名字 `<X>`,board `<Y>`,工具链 `<Z>`,确认吗?" |
| 任务要切工具链 | "当前 project 工具链是 `<X>`,不允许我改。请人决策。" |
| 任务要改 PIO 平台/NCS 版本 | "升级 PIO espressif32 / NCS 版本是架构级改动,请人决策。" |

---

## 自我检查清单(开始任务前过一遍)

- [ ] 我要改的文件是不是只在**一个** `projects/<name>/` 下?
- [ ] 我用的工具链对得上 project 命名前缀?
- [ ] 我没有动 `contracts/`、`docs/`、根 `README.md`?
- [ ] 我没有新建 `lib/`、`shared/`、`common/` 顶级目录?
- [ ] 我没有引入 west/idf.py/cmake 跨 project 的直接调用?

**任何一项答 "否",停下来问人**。

---

## 实测踩坑沉淀(给后续 AI)

| 坑 | 触发条件 | 修复 |
|---|---|---|
| PIO + ESP-IDF configure 阶段 git_describe 失败 | 项目根 CMakeLists.txt 没 `set(PROJECT_VER "...")` | 显式写死 PROJECT_VER |
| PIO Zephyr import yaml 失败 | PIO 安装时没带 pyyaml | `uv tool install platformio --with pyyaml --with west` —— **但即使修了,Zephyr 还是 2.7.1 没用** |
| ESP-IDF 把 `src_dir = projects` 当单一组件 | 用 monorepo 单根 platformio.ini 想跨 project | **行不通,每个 ESP-IDF project 必须自己一份 platformio.ini** |
| 在 mote/zephyr/ 子目录放 prj.conf | PIO Zephyr 期望那里它自己管 | mote 走 west 后,`prj.conf` 在 project 根目录 |

---

## 给你的元规则

这份文档比代码值钱。**如果你发现这份文档过时或矛盾,先告诉用户**,不要自作主张按"看起来正确"的方式工作。文档迭代是协作的一部分,不是你的责任。
