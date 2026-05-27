# mote

**Role**: BLE 节点(电池供电、深度睡眠、事件广播)
**Function**: `motion` — IMU 触发的动作检测
**Chip**: nRF52840 — west + NCS 工具链
**Board**: Seeed XIAO nRF52840 Sense (`xiao_ble/nrf52840/sense`)
**Status**: LSM6DS3TR-C WAKE_UP / INACTIVITY 硬件中断驱动,静默时不广播;boot 发 `moving=0` BTHome heartbeat,动作触发发 `moving=1` burst,随后打开 30s connectable 配置窗口。Release 默认 RTT 日志,`./dev mote build --debug` 打开 USB CDC。

## LED 交互

板载 RGB LED 按状态点亮(GPIO on/off,无 PWM 调光),待机常灭以省电:

| 状态 / 触发 | 颜色 | 亮时长 | 节奏 | 固件 |
|---|---|---|---|---|
| Boot 上电 | 白 (R+G+B) | 80 ms | 单闪一次 | release + debug |
| 待机静默 | 灭 | — | 常灭 | release + debug |
| 动作事件广播 | 绿 | 80 ms | 每次事件一脉冲 | release + debug |
| 配置窗口(30s connectable) | 蓝 | 30 ms | 每 5 s 一次慢闪 | release + debug |
| Web BT 已连接 | 青 (G+B) | 80 ms | 每 2 s 一次慢闪 | release + debug |
| IMU IRQ | 黄 (R+G) | 25 ms | 每次 IRQ 一脉冲 | 仅 debug (USB CDC) |

> 绿闪只在事件**真正广播**时触发(motion gate 放行后);config / connected 两种慢闪互斥,进配置窗口先蓝闪,Web BT 连上后切青闪。

## BTHome 广播对象

| Object | object_id | Type |
|--------|-----------|------|
| packet_id | 0x00 | uint8 |
| moving | 0x22 | uint8 (bool) |

`moving=1` 表示动作事件,`moving=0` 表示 boot heartbeat。`packet_id` 是 multi-gateway consumer dedup key;同一事件 burst 内复用同一 payload。

## IMU 轴向(经验测出,LSM6DS3TR-C)

XIAO Sense 上 LSM6DS3TR-C 的 chip 坐标系相对板子物理方向(**平放 USB-C 朝你,组件面朝上** 为参考姿态):

| Chip 轴 | 板上方向 |
|---|---|
| **+X** | 远端 BAT 焊盘方向 |
| **-X** | 近端 USB-C 方向 |
| **+Y** | 左长边方向(组件面朝上时你的左手边) |
| **-Y** | 右长边方向 |
| **+Z** | 出组件面(朝天) |
| **-Z** | 进 PCB(朝地) |

右手系自验证:`+X × +Y = +Z` ✓

参考姿态静态读数:`ax ≈ 0, ay ≈ 0, az ≈ +1000 mg`,L1 magnitude ≈ 1000 mg。

**算法影响**:固件以芯片硬件 WAKE_UP slope 检测作为主触发,触发后短采样 raw XL 读数计算连续样本 L1 delta score,用于日志与 source bit 缺失时的兜底。事件发送由 motion gate 的 2s cooldown 去重,不再依赖 INACTIVITY 一定到来才允许下一次事件。

## 第一次 bootstrap(一次性,~4 GB 下载)

本 project 使用**外置 NCS workspace**,不在仓库内执行 `west init`。

需要先在 `~/ncs/v2.9.2/` 安装好 NCS v2.9.2(nrfutil + NCS toolchain):见根 `docs/build.md`。

安装完成后直接编译即可。

## 编译 / 烧录 / 监视

```bash
# 从仓库根目录:
./dev mote build                              # 编译 UF2
./dev mote flash                              # 拷贝 UF2 到 /Volumes/XIAO-SENSE/
./dev mote log                                # 串口日志(tio)
./dev mote run                                # flash + log

# 覆盖默认值:
./dev mote build --ncs v2.9.2
./dev mote flash --volume /Volumes/XIAO-SENSE
./dev mote log --port /dev/cu.usbmodem101

# 直接调用 west(仅当 ./dev 不够用时):
cd mote
ZEPHYR_BASE=~/ncs/v2.9.2/zephyr nrfutil toolchain-manager launch --ncs-version v2.9.2 -- \
  west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build_uf2
cp build_uf2/zephyr/zephyr.uf2 /Volumes/XIAO-SENSE/

# SWD 烧录备选(需 J-Link 或 CMSIS-DAP):
ZEPHYR_BASE=~/ncs/v2.9.2/zephyr nrfutil toolchain-manager launch --ncs-version v2.9.2 -- west flash
```

上电后 5 秒内 Adafruit bootloader 自动挂载 `/Volumes/XIAO-SENSE/`。若错过窗口,双击 RESET 重新进入 bootloader 模式。

## 改业务在哪

| 改动 | 文件 |
|---|---|
| 应用逻辑 | `src/main.c`(新增 `.c` 在 `CMakeLists.txt` 的 `target_sources` 追加) |
| Zephyr 配置 | `prj.conf` |
| 板特化 DTS overlay | `app.overlay`(LSM6DS3TR-C 通过 `imu` alias 访问) |
| 加 NCS 模块 | `west.yml` + `west update` |
