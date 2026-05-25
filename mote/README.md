# mote

**Role**: BLE 节点(电池供电、深度睡眠、事件广播)
**Function**: `motion` — IMU 触发的动作检测
**Chip**: nRF52840 — west + NCS 工具链
**Board**: Seeed XIAO nRF52840 Sense (`xiao_ble/nrf52840/sense`)
**Status**: LSM6DS3TR-C IMU 实时采样,状态机驱动 non-connectable BTHome v2 Service Data 广播(UUID=`0xFCD2`,对象映射见 `AGENTS.md §5`),USB CDC 日志(product=`seeedmote-motion`,VID=0x2886,PID=0x0045)。

## BTHome 广播对象

| Object | object_id | Type |
|--------|-----------|------|
| packet_id | 0x00 | uint8 |
| moving | 0x22 | uint8 (bool) |
| vibration | 0x2C | uint8 (bool) |
| count | 0x3E | uint32 |

`count` 每业务事件递增一次,不是 heartbeat。`packet_id` 用于 BLE 链路去重。

## 第一次 bootstrap(一次性,~4 GB 下载)

本 project 使用**外置 NCS workspace**,不在仓库内执行 `west init`。

需要先在 `~/ncs/v2.9.2/` 安装好 NCS v2.9.2(nrfutil + NCS toolchain):见根 `docs/build.md`。

安装完成后直接编译即可。

## 编译 / 烧录 / 监视

```bash
# 从仓库根目录:
make build                                     # 编译 UF2
make flash                                     # 拷贝 UF2 到 /Volumes/XIAO-SENSE/
make monitor                                   # 串口日志(tio)
make run                                       # flash + monitor

# 覆盖默认值:
make NCS_VERSION=v2.9.2 build
make UF2_VOLUME=/Volumes/XIAO-SENSE flash
make PORT=/dev/cu.usbmodem101 monitor

# 直接调用 west(仅当 Makefile 不够用时):
cd mote
ZEPHYR_BASE=~/ncs/v2.9.2/zephyr west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build_uf2
cp build_uf2/zephyr/zephyr.uf2 /Volumes/XIAO-SENSE/

# SWD 烧录备选(需 J-Link 或 CMSIS-DAP):
ZEPHYR_BASE=~/ncs/v2.9.2/zephyr west flash
```

上电后 5 秒内 Adafruit bootloader 自动挂载 `/Volumes/XIAO-SENSE/`。若错过窗口,双击 RESET 重新进入 bootloader 模式。

## 改业务在哪

| 改动 | 文件 |
|---|---|
| 应用逻辑 | `src/main.c`(新增 `.c` 在 `CMakeLists.txt` 的 `target_sources` 追加) |
| Zephyr 配置 | `prj.conf` |
| 板特化 DTS overlay | `app.overlay`(LSM6DS3TR-C 通过 `imu` alias 访问) |
| 加 NCS 模块 | `west.yml` + `west update` |
