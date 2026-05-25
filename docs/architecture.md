# Architecture — SeeedMote v2

一页总览。决策细节见 git log + 根 `AGENTS.md`。

## 它是什么

Seeed 方案商团队的**参考架构家族骨架**。BLE 低功耗节点 + 网关聚合 + MQTT 出口,衍生具体方案(冷链、急停、接触、资产追踪 …),通过 [SenseCraft Solution 平台](https://github.com/suharvest/app_collaboration) 投放集成商。

## 两盒一契约

```
 ┌────────────────────────┐  BLE adv   ┌────────────────────────┐  MQTT   ┌──────────────┐
 │  Mote                  │ ─────────▶ │  Gateway               │ ──────▶ │ Consumer     │
 │  XIAO nRF52840 Sense   │  BTHome v2 │  XIAO ESP32-S3         │         │  HA / MQTT / │
 │  Zephyr (NCS)          │            │  ESPHome               │         │  自有后端    │
 │  via west              │            │  原生 BTHome 支持       │         │  消费侧去重   │
 │  System OFF ≤ 5 µA*    │            │  WiFi → MQTT           │         │              │
 └────────────────────────┘            └────────────────────────┘         └──────────────┘
                                                                            * 业务接入后
```

默认空中格式是 **BTHome v2 BLE advertising**。Mote 不建立长连接,通过 Service Data UUID `0xFCD2` 广播标准 `packet id` / `moving` / `vibration` / `count` 对象。

**Gateway 是 ESPHome**:原生支持 BTHome v2 passive scan、WiFi STA、MQTT 出口、OTA。无需自研 C 固件。配置即文档:`gateway/esphome.yaml`。

## 事件型(event-driven),不是数据型 IoT

整个系统是**事件型**。这条原则约束所有实现:

| 维度 | 数据型 IoT(我们不是) | **SeeedMote(事件型)** |
|---|---|---|
| 上行触发 | 周期采样 / 心跳 | 业务条件触发才广播 |
| Payload 性质 | 数值时序 | 业务语义对象(moving / vibration / count) |
| Mote 静默期 | 异常("丢数据") | 正常("没事发生") |
| Gateway 角色 | 数据采集汇聚 | 事件中继(stateless) |
| Broker | 时序数据传输 | 事件总线(不持久化) |
| 消费侧 | 时序图 / 阈值告警 | 事件流 / 状态机派生 / 业务响应 |
| **不要的东西** | n/a | raw 采样缓存 / 周期 telemetry / 心跳 topic |

**如果你发现自己在加周期发布、心跳计数器、raw 缓存,你走错路了。**

## 链路(4 段)

```
 ┌────────┐  BLE adv  ┌─────────┐  MQTT pub  ┌────────┐  MQTT sub  ┌─────────┐
 │  Mote  │──事件────▶│ Gateway │──事件─────▶│ Broker │──事件─────▶│   App   │
 │ (电池) │  BTHome   │ ESP32S3 │            │(外部)  │            │ 消费侧  │
 └────────┘           └─────────┘            └────────┘            └─────────┘
   触发源              ESPHome 中继             总线                 消费 + 展示
```

## 工具链决策(已锁定)

| 对象 | 工具链 | 理由 |
|---|---|---|
| Mote (nRF52840) | west + NCS | NCS 是 nRF 系列官方现代 SDK;PIO 的 Zephyr 卡在 2.7.1(2021),实测不可用 |
| Gateway (ESP32-S3) | ESPHome | 原生 BTHome v2 支持,WiFi+MQTT+OTA 内置,无需维护 C 固件 |

旧 ESP-IDF C gateway 已删除(见 git log 历史)。

## 开源生态兼容性

- **Home Assistant**: ESPHome 原生集成,BTHome v2 设备自动发现
- **任意 MQTT broker**: Mosquitto / EMQX / HiveMQ / CloudMQTT 均可
- **自有后端**: 订阅 MQTT topic,按 `(mote_mac, count)` 去重即可消费事件

## 当前显式不做的事

| 项 | 状态 |
|---|------|
| OTA / MCUBoot (mote) | v2.0 显式放弃,v2.1 由人决策 |
| 多 app 共存 / framework/bsp 分层 | 本骨架反向回归"重复 > 抽象" |
| 端到端加密 / 多租户 / ACL | v2.1+ |
| 网关间协调 / 选主 / 集群去重 | 显式拒绝,消费侧按 BTHome `count` + 设备地址去重 |
| **周期 telemetry / raw 数据流上报** | **显式拒绝** —— 事件型系统 |

## 与 SenseCraft Solution 平台的关系

- 平台仓库:[`suharvest/app_collaboration`](https://github.com/suharvest/app_collaboration)
- SeeedMote v2 **不是方案集**,**是平台的一个 candidate**。业务跑通后以 `seeedmote_ble_node/` 进 `solutions/`
