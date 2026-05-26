---
name: seeedmote-dev
description: "SeeedMote v2 仓库的开发入口。通过仓库根 ./dev 调用 mote(west+NCS)、gateway(ESPHome YAML)、app demo 和 doctor 检查。用于:编译 build / 烧录 flash / 看日志 log/monitor / 一键 run / 清理 clean / gateway / app / nrf52 / esp32 / xiao / west / esphome。"
allowed-tools: Bash(./dev:*), Bash(python3:*), Bash(ls:*)
---

# SeeedMote v2 — Dev Commands

仓库根 `./dev` 是人用统一入口。根 `Makefile` 只是兼容转发层,不要把它当主入口。

从仓库根目录运行命令,不要 `cd` 进 `mote/` 或 `gateway/` 后再手动选工具链。

## 对象选择

| 对象 | 工具链 | 入口 |
|---|---|---|
| `mote` | west + NCS v2.9.2 | `./dev mote ...` |
| `gateway` | ESPHome YAML | `./dev gateway ...` |
| `app` | FastAPI + React demo | `./dev app ...` |

Gateway 是 ESPHome YAML,不写 C / PlatformIO / ESP-IDF 固件。

## 常用命令

```bash
./dev doctor

./dev mote build
./dev mote build --debug
./dev mote flash
./dev mote log
./dev mote run --debug
./dev mote clean

./dev mote build --ncs v2.9.2
./dev mote flash --volume /Volumes/XIAO-SENSE
./dev mote log --port /dev/cu.usbmodem101

./dev gateway compile
./dev gateway run
./dev gateway log

./dev app run
./dev app run --mock
```

## 决策树:用户让我"烧录/编译/看日志"时

1. **看对象**:
   - 提 "mote" / "motion" / "nrf52" / "XIAO BLE" → `./dev mote ...`
   - 提 "gateway" / "ESP32-S3" / "ESPHome" → `./dev gateway ...`
   - 提 "app" / "demo" / "UI" → `./dev app ...`
2. **看动词**:
   - 编译 / compile / build → `./dev mote build` 或 `./dev gateway compile`
   - 烧录 / flash / upload / 下载 → `./dev mote flash` 或 `./dev gateway run`
   - 看串口 / 看日志 / monitor / serial → `./dev mote log` 或 `./dev gateway log`
   - 跑起来 / 直接看 → `./dev mote run` / `./dev gateway run` / `./dev app run`
   - 清理 / clean → `./dev mote clean`
   - 检查环境 → `./dev doctor`
3. **mote flash 之前**:
   - `./dev mote flash` 会先尝试 1200-baud touch。
   - Release 固件没有 USB CDC 时,按输出提示双击 RESET 让 `/Volumes/XIAO-SENSE/` 出现。

## 不要做的事

- ❌ 不要在 `gateway/` 写 C / CMake / PlatformIO。
- ❌ 不要给 nRF mote 跑 `pio run`。
- ❌ 不要给 gateway 跑 `west build`。
- ❌ 不要把根 `Makefile` 当成主入口继续扩展;新增体验优先加到 `./dev`。
- ❌ 不要频繁 `west update`;只有 `mote/west.yml` 改了才需要。

## 故障排查

- **`nrfutil: command not found`** → 参考 `docs/build.md` 安装 nrfutil + toolchain-manager。
- **`west: command not found` 但有 `nrfutil`** → 用 `./dev`,它会通过 `nrfutil toolchain-manager launch --ncs-version ... -- west` 启动。
- **`/Volumes/XIAO-SENSE` 找不到** → mote 没进 bootloader,双击 RESET。
- **串口找不到** → 跑 `./dev doctor` 看 `/dev/cu.*`;或 `./dev mote log --port /dev/cu.usbmodem101`。
- **Gateway secrets 缺失** → `cp gateway/secrets.yaml.example gateway/secrets.yaml` 后填 WiFi/MQTT。
