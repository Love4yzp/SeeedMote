# mote_motion_nrf52840

**Role**: `mote` —— BLE 节点(电池供电、深度睡眠、事件广播)
**Function**: `motion` —— IMU 触发的动作检测(未来扩展时会有 `env`、`button`、`contact` 等其他 function)
**Chip**: `nrf52840` —— **决定走 west + NCS 工具链**
**Board**: Seeed XIAO nRF52840 Sense(Zephyr board id: `xiao_ble`)
**Status**: hello blink only — 无 BLE、无传感器、无 System OFF。

## ⚠️ 这个 project 用 west(因为 chip 是 nrf52840)

仓库**双轨制**,**工具链看 chip 不看 role**:
- chip 是 `nrf52*` → west + NCS
- chip 是 `esp32*` → PlatformIO + ESP-IDF

如果未来出现 `mote_motion_esp32c6`(把 mote 建在 ESP32-C6 上),**它走 PIO**,不是这条路。

PIO 的 Zephyr framework 是 2.7.1(2021),实测不可用,所以 nRF 系列必须 native NCS。

**AI 不能自作主张切换工具链**。

## 第一次 bootstrap(一次性,~4 GB 下载)

```bash
cd projects/mote_motion_nrf52840
west init -l .
west update          # 下载 NCS v2.9.2 + Zephyr 3.7 + nrfxlib + MCUBoot 等
```

需要先装好 nrfutil + NCS toolchain:见根 `docs/build.md`。

## 改业务在哪

- 应用代码:`src/main.c`(新增 `.c` 在 `CMakeLists.txt` 的 `target_sources` 追加)
- Zephyr 配置:`prj.conf`
- 板特化 DTS overlay(如需):`app.overlay`(目前不需要)

**不要动的目录**:本 project 之外的任何文件。改 `contracts/`、改别的 project、改根 `README.md`、改 `docs/` 都要先问人。

## 编译 / 烧录 / 监视

```bash
cd projects/mote_motion_nrf52840
west build -b xiao_ble                           # 编译
west build -b xiao_ble -- -DCONF_FILE=prj.conf   # 显式 conf

# 烧录(优先 UF2,双击 RESET 进 XIAOBOOT 模式)
cp build/zephyr/zephyr.uf2 /Volumes/XIAOBOOT/

# 备选:SWD 烧录(需 J-Link 或 CMSIS-DAP)
west flash

# RTT 日志查看(SWD 探针)
JLinkRTTViewer
# 或:JLinkExe + RTT telnet
```

## 永远不要在这里运行

```
❌ pio run -e mote_motion_nrf52840         # 没有 platformio.ini
❌ pio run -d ../mote_motion_nrf52840      # 即使有也是错路
```

PIO + Zephyr 在我们环境实测不可行,**整体结论见 `docs/architecture.md`**。
