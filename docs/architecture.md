# Architecture — SeeedMote v2

一页总览。决策细节见 git log + `for-ai-agents.md`。

## 它是什么

Seeed 方案商团队的**参考架构家族骨架**。BLE 低功耗节点 + 多网关聚合 + 出口契约,衍生具体方案(冷链、急停、接触、资产追踪 …),通过 [SenseCraft Solution 平台](https://github.com/suharvest/app_collaboration) 投放集成商。

## 两盒一契约

```
 ┌────────────────────────┐  BLE adv   ┌────────────────────────┐  MQTT   ┌──────────────┐
 │  Mote                  │ ─────────▶ │  Gateway               │ ──────▶ │ Consumer     │
 │  XIAO nRF52840 Sense   │            │  XIAO ESP32-S3         │         │  HA / MQTT / │
 │  Zephyr (NCS v2.9.2)   │            │  ESP-IDF (5.2.2)       │         │  自有后端    │
 │  via west              │            │  via PlatformIO        │         │  消费侧去重   │
 │  System OFF ≤ 5 µA*    │            │  常电、stateless 转发  │         │              │
 └────────────────────────┘            └────────────────────────┘         └──────────────┘
                                                                            * 业务接入后
```

## 三个独立维度

| 维度 | 内容 | 决定 |
|---|---|---|
| **Role**(职能)| `mote` / `gateway` / 未来的 `bridge` / `edge_ai` 等 | 项目做什么 |
| **Function**(功能)| `motion` / `basic` / 未来的 `env` / `button` 等 | 业务子类型 |
| **Chip**(芯片家族)| `nrf52840` / `esp32s3` / `esp32c6` / `rp2040` 等 | 跑在什么硬件上 |

Project 名格式:`<role>_<function>_<chip>`(如 `mote_motion_nrf52840`、`gateway_basic_esp32s3`)。
**工具链由 chip 决定,与 role 无关**。

## 双轨工具链(实测后的决策)

最初的设想是"PIO 唯一入口"。**实测后被推翻**:

| 期望 | 实测结果 |
|---|---|
| PIO 管 ESP32 / ESP-IDF | ✅ 完美:5.2.2 现代版本,modern API 完整,板 JSON 内置 |
| PIO 管 nRF52 / Zephyr | ❌ 失败:PIO 装的是 Zephyr **2.7.1(2021)**、`<zephyr.h>` 老 API、PIO 框架脚本多个 bug、无 XIAO BLE 板 |

**规则**(2026-05 实测后定):

| 芯片家族 | 工具链 |
|---|---|
| `nrf52*` | **west + nRF Connect SDK (NCS v2.9.2)** |
| `esp32*` | **PlatformIO + ESP-IDF (5.2.2)** |
| `rp2040` / `stm32*` / 未来其他 | **未定,出现时人决策** |

**关键认知**:
- 一个 `mote_*` 不强制是 nRF52,它**可能**未来出现在 ESP32-C6 上(那就走 PIO)
- 一个 `gateway_*` 不强制是 ESP32,它**可能**未来是低功耗 BLE 中继 on nRF52(那就走 west)
- Role 是项目描述,**chip 才是工具链开关**
- AI 看 chip 后缀决定工具链,**不允许自选**

## 五条铁律

| # | 铁律 | 为什么 |
|---|------|--------|
| 1 | **`projects/` 平铺,每个 project 独立可编** | 单 AI 任务只看一个目录,孤岛结构防漂移 |
| 2 | **工具链由 chip 后缀决定,与 role 无关** | `_nrf52*` = west,`_esp32*` = PIO。role 只是命名描述 |
| 3 | **无 `lib/` 共享层** | 第三个 project 才有数据点判断真复用 |
| 4 | **role / chip / function 是三个独立维度** | 不建 `mote/` `gateway/` 顶层目录,组合在命名里表达 |
| 5 | **`contracts/` 本骨架不写内容** | 跨设备契约要真业务验证才知道字段 |

## 与 v2.0 仓库的关系

- v2.0:`~/Seeed/dev/embedded/`,单 app + framework/bsp 分层,**已是 west + PIO 双构建** —— 但混在一个仓库里,AI 没纪律
- v2(本仓库):`~/Seeed/dev/seeedmote-v2/`,平铺 `projects/`,**双轨明确分离**,每个 project 单一工具链绑死
- v2.0 业务代码作为参考保留,**不迁移** —— 等本骨架的 AI 流程跑通,再按本仓库纪律重写

## 与 SenseCraft Solution 平台的关系

- 平台仓库:[`suharvest/app_collaboration`](https://github.com/suharvest/app_collaboration)。已有 18 个方案、Desktop App、OTA、CLI
- SeeedMote v2 **不是方案集**,**是平台的一个 candidate**。业务跑通后以 `seeedmote_ble_node/` 进 `solutions/`
- 接入过程预期暴露平台三个缺口,可作为反哺:
  1. MCU/电池类 deployer(`nrf52_uf2`、`ble_oob_provision`)
  2. 节点能力契约(`capability` schema)
  3. `component` 类型方案(节点固件可被多个具体方案 `composes:`)

## 演进路径(粗粒度,不预先建)

```
 Now              第 1 个真业务      第 2 个 project      第 3 个 project        反哺平台
 ────            ────────────────  ────────────────    ────────────────       ──────────
 骨架建成    →   mote 加 BLE adv  → gateway 加 BLE   →  抽 contracts/     →   提交 SenseCraft
                + 一个传感器         scan + MQTT 出口     airframe.yaml         Solution
```

每一步等上一步业务跑通再走,**不预先抽象**。

## 当前显式不做的事

| 项 | 状态 |
|---|------|
| OTA / MCUBoot / mcumgr | v2.0 显式放弃,v2.1 由人决策 |
| 多 app 共存 / framework/bsp 分层 | v2.0 过早抽象,本骨架反向回归"重复 > 抽象" |
| 共享 `lib/` 层 | 第三个 project 才考虑 |
| `tools/codegen` / `tools/scaffold` | 没有第二个 mote 形态前不做 |
| 端到端加密 / 多租户 / ACL | v2.1+ |
| 网关间协调 / 选主 / 集群去重 | 显式拒绝,消费侧 `(device_id, boot_uuid, event_counter)` 去重 |

## 实测验证(2026-05)

| 项 | 状态 | 详情 |
|---|---|---|
| Gateway PIO + ESP-IDF 编译 | ✅ 通过 | 234 KB firmware.bin,22.4% flash,需 `set(PROJECT_VER ...)` 绕 git_describe |
| Mote west + NCS 编译 | ⏳ 结构对齐 v2.0 已工作样式 | 实际 `west update` (~4 GB) 留待业务开发时触发 |
| PIO + Zephyr(预想路线) | ❌ 推翻 | Zephyr 2.7.1 太旧、bug 多、无 XIAO 板 —— 死路 |
