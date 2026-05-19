# gateway_basic_esp32s3

**Role**: `gateway` —— BLE 网关(常电、扫描 BLE 广播、转发到 MQTT)
**Function**: `basic` —— 基础 MQTT 出口(未来扩展时会有 `webhook`、`opcua`、`grpc` 等其他 function)
**Chip**: `esp32s3` —— **决定走 PlatformIO + ESP-IDF 工具链**
**Board**: Seeed XIAO ESP32-S3(PIO board id: `seeed_xiao_esp32s3`)
**Status**: BLE observer + UART JSON 出口 —— NimBLE passive scan,解码 BTHome v2 Service Data(UUID=`0xFCD2`)中 [`contracts/airframe.yaml`](../../contracts/airframe.yaml) 声明的 motion profile,每帧一行 JSON 到 UART。无 Wi-Fi、无 MQTT。

## ⚠️ 这个 project 用 PIO(因为 chip 是 esp32s3)

仓库**双轨制**,**工具链看 chip 不看 role**:
- chip 是 `esp32*` → PlatformIO + ESP-IDF
- chip 是 `nrf52*` → west + NCS

如果未来出现 `gateway_basic_nrf52840`(把 gateway 建在 nRF52 上),**它走 west**,不是这条路。

**AI 不能自作主张切换工具链**。

## 改业务在哪

| 改动 | 文件 |
|---|---|
| 新业务 .c | `src/*.c` + `src/CMakeLists.txt` 的 `SRCS` 追加 |
| 新 ESP-IDF 组件依赖 | `src/CMakeLists.txt` 的 `REQUIRES` 追加 |
| 启用 BT/WiFi/lwIP 等 | `sdkconfig.defaults` |
| 改 app 版本号 | `CMakeLists.txt`(项目根)的 `set(PROJECT_VER ...)` |
| 改 espressif32 平台版本 | `platformio.ini`(架构级,问人) |

**不要动的目录**:本 project 之外的任何文件。改 `contracts/`、改 `mote_motion_nrf52840/`、改 `docs/` 都要先问人。

## 编译 / 烧录 / 监视

```bash
# 从仓库根目录
pio run -d projects/gateway_basic_esp32s3                   # 编译
pio run -d projects/gateway_basic_esp32s3 -t upload         # 烧录 (USB CDC)
pio device monitor -d projects/gateway_basic_esp32s3        # 串口监视 (115200)
pio run -d projects/gateway_basic_esp32s3 -t clean          # 清产物
```

## 文件作用

| 文件 | 作用 |
|---|---|
| `platformio.ini` | PIO env 定义(本 project 唯一) |
| `CMakeLists.txt`(根) | ESP-IDF 项目顶层,**`set(PROJECT_VER ...)`必须**(绕过 git_describe) |
| `src/main.c` | app 入口 |
| `src/CMakeLists.txt` | main 组件清单(SRCS + REQUIRES) |
| `sdkconfig.defaults` | ESP-IDF 默认配置(版本固定、子系统开关) |

## XIAO ESP32-S3 板上 LED

GPIO21,active-low(写 0 点亮,写 1 熄灭)。

## 永远不要在这里运行

```
❌ west build              # 没有 west.yml
❌ idf.py build            # ESP-IDF 由 PIO 调度,不暴露原生入口
```

## 下一步会做的事(不在本骨架范围)

按根 `docs/for-ai-agents.md` 操作手册接业务任务。
