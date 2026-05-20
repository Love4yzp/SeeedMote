---
name: seeedmote-dev
description: "SeeedMote v2 仓库的编译/烧录/串口/清理统一入口。覆盖双轨工具链(west+NCS for nrf52*, PlatformIO+ESP-IDF for esp32*),通过仓库根 Makefile 调用。用于:编译 build / 烧录 flash / 监视串口 monitor / 一键跑 run / 清理 clean / 擦除 erase 任何 projects/<name>/ 下的 firmware。触发词:build / compile / 编译 / flash / upload / 烧录 / 烧写 / 下载固件 / monitor / serial / 串口 / 监视日志 / clean / 清理 / erase / 擦除 / gateway / mote / esp32 / nrf52 / xiao / pio / west / platformio。"
allowed-tools: Bash(make:*), Bash(ls:*)
---

# SeeedMote v2 — Dev Commands

仓库根 `Makefile` 是统一入口,自动按 `PROJECT` 后缀分发到 west 或 PIO。**始终从仓库根目录(houston/)运行 `make`,不要 `cd` 进 `projects/<name>`**。

## 项目选择

| PROJECT 值 | 工具链 | 当前默认行为 |
|---|---|---|
| `gateway_basic_esp32s3` (**default**) | PlatformIO + ESP-IDF | `make` 不带参数即跑这个 |
| `mote_motion_nrf52840` | west + NCS v2.9.2 | 必须显式 `PROJECT=mote_motion_nrf52840` |

判定规则(写死,不要质疑):**看 chip 后缀,不看 role 前缀**。`_nrf52*` → west;`_esp32*` → PIO。详见 `docs/for-ai-agents.md`。

## Target 速查

| Target | 含义 | PIO 路径 | west 路径 |
|---|---|---|---|
| `make build` | 仅编译 | `pio run -d projects/<P>` | `west build --no-sysbuild -p always -b <board> -d build_uf2` |
| `make flash` | 编译 + 上传 | `pio run -t upload` | 拷贝 `zephyr.uf2` 到 `/Volumes/XIAO-SENSE/` |
| `make monitor` | 打开串口 | `pio run -t monitor` | `tio /dev/cu.usbmodem*` |
| `make run` (= `make`) | flash + monitor | 一次跑完 | flash + tio |
| `make clean` | 清理产物 | `pio run -t clean` | `west build -t pristine` |
| `make erase` | 擦除 flash | `pio run -t erase` | **不支持**(UF2 流程无 SWD) |

## 常用命令

```bash
# Gateway(ESP32-S3)默认 project,最常用
make build
make flash
make monitor
make            # = make run = flash + monitor

# Mote(nRF52840)— 必须显式 PROJECT
make PROJECT=mote_motion_nrf52840 build
make PROJECT=mote_motion_nrf52840 flash    # 需要双击 reset 让 /Volumes/XIAO-SENSE 出现
make PROJECT=mote_motion_nrf52840 monitor

# 指定串口(PIO 也会自动探测,通常不需要)
make PORT=/dev/cu.usbmodem101 flash
make PROJECT=mote_motion_nrf52840 PORT=/dev/cu.usbmodem101 monitor

# 改 UF2 挂载点(默认 /Volumes/XIAO-SENSE)
make PROJECT=mote_motion_nrf52840 UF2_VOLUME=/Volumes/XIAO-SENSE flash

# 切 NCS 版本(默认 v2.9.2,在 ~/ncs/<ver>/zephyr)
make PROJECT=mote_motion_nrf52840 NCS_VERSION=v2.9.2 build
```

## 决策树:用户让我"烧录/编译/看日志"时

1. **看用户提到的是 gateway 还是 mote**:
   - 没提 / 提"gateway" → 默认 PROJECT(`gateway_basic_esp32s3`),不传 `PROJECT=`
   - 提"mote" / "motion" / "nrf52" / "XIAO BLE" → 加 `PROJECT=mote_motion_nrf52840`
2. **看动词**:
   - 编译 / compile / build → `make build`
   - 烧录 / flash / upload / 下载 → `make flash`
   - 看串口 / 看日志 / monitor / serial → `make monitor`
   - 跑起来 / 直接看 → `make` 或 `make run`
   - 清理 / clean → `make clean`
   - 擦 flash / 重置 → `make erase`(仅 ESP32,nRF52 会报错)
3. **mote flash 之前**:提醒用户**双击 reset 按钮**让 `/Volumes/XIAO-SENSE/` 出现(5 秒窗口),否则 `cp` 会失败。

## 不要做的事

- ❌ 不要 `cd projects/<name>` 然后再跑 `pio` / `west` ——直接在根目录用 `make`
- ❌ 不要给 nRF 项目跑 `pio run`(看 chip 后缀)
- ❌ 不要给 ESP 项目跑 `west build`(同上)
- ❌ 不要自己改 `Makefile` 的工具链分发逻辑;新增 project 只要命名后缀正确就会自动走对路径
- ❌ 不要为了"省事"在 mote 上调 `make erase`(Makefile 会主动报错退出)

## 故障排查

- **`pio: command not found`** → 没装 PlatformIO,跑 `pipx install platformio` 或参考 `docs/build.md`
- **`west: command not found` 但有 `nrfutil`** → Makefile 默认用 `nrfutil toolchain-manager launch --ncs-version v2.9.2 -- west`,确认 `nrfutil` 装了 `toolchain-manager` 组件
- **`/Volumes/XIAO-SENSE: No such file`** → mote 没进 bootloader,**双击 reset**
- **flash 后串口找不到** → ESP32 上传后会断开重连,等 1-2 秒再 `make monitor`;或直接 `make run` 一次跑完
- **UART 没输出但 BLE 在广播** → 检查 `projects/<P>/src/` 里的日志级别,不是 Makefile 问题
