# Build — 工具链与命令清单

**双轨制**:**工具链由芯片家族决定**,不由 role 决定:

```
projects/<any role>_*_nrf52*/     →  west + nRF Connect SDK (NCS v2.9.2)
projects/<any role>_*_esp32*/     →  PlatformIO + ESP-IDF (5.2.2)
projects/<any role>_*_rp2040/     →  未定,问人
```

Project 名格式:`<role>_<function>_<chip>`(例 `mote_motion_nrf52840`、`gateway_basic_esp32s3`)。
**编译看 `_<chip>` 后缀**,role 只是命名描述。**不要让 AI 自己选轨**。

---

## Mote 侧 · west + NCS

### 一次性安装(macOS Apple Silicon 流程)

```bash
# nrfutil + NCS toolchain
curl -sSL -o ~/.local/bin/nrfutil \
    https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/aarch64-apple-darwin/nrfutil
chmod +x ~/.local/bin/nrfutil
sudo mkdir -p /opt/nordic && sudo chown -R $USER /opt/nordic
nrfutil install toolchain-manager device
nrfutil toolchain-manager install --ncs-version v2.9.2
```

完整指引参考 v2.0 仓库 `docs/dev-setup.md`(同款流程)。

### NCS workspace 说明

NCS v2.9.2 安装在 `~/ncs/v2.9.2/`(由 nrfutil toolchain-manager 管理,共享,不在 project 目录里)。

编译时通过 `ZEPHYR_BASE` 指向它,**无需在 project 目录下跑 `west init` / `west update`**。

### 编译 / 烧录 / 监视

```bash
cd projects/mote_motion_nrf52840

# 编译 UF2 bootloader 可直接运行的镜像
ZEPHYR_BASE=~/ncs/v2.9.2/zephyr west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build_uf2

# 清编译
west build -d build_uf2 -t pristine

# 烧录:首选 UF2 拖拽
# 上电后 5 秒内 /Volumes/XIAO-SENSE/ 自动出现(Adafruit bootloader 上电即有 DFU 窗口)
cp build_uf2/zephyr/zephyr.uf2 /Volumes/XIAO-SENSE/
# Fallback:如果 app 已正常运行想进 bootloader,双击 XIAO 板上的 RESET 按钮

# 备选:SWD(需要 J-Link / CMSIS-DAP)
west flash

# 监视日志
# 首选:USB CDC(mote 上电后出现 /dev/cu.usbmodem*)
tio /dev/cu.usbmodem*
# 备选:RTT(SWD 探针)
JLinkRTTViewer
```

### 不要做的事(west 侧)

- ❌ 在 mote project 里跑 `pio run` —— 没 platformio.ini
- ❌ `west update` 频繁运行 —— 每次都拉网络,只在 west.yml 改了才跑
- ❌ 改 NCS 版本(`west.yml` 的 `revision`)—— 架构级,问人

---

## Gateway 侧 · PlatformIO + ESP-IDF

### 一次性安装

```bash
# PIO Core(macOS)
brew install platformio
# 或
uv tool install platformio --with pyyaml --with west

pio --version    # 至少 6.x
```

PIO 会按需自动下载 `espressif32@6.7.0` platform 包 + ESP-IDF 5.2.2 工具链(首次 ~1 GB)。

### 编译 / 烧录 / 监视

```bash
# 从仓库根
pio run -d projects/gateway_basic_esp32s3                  # 编译
pio run -d projects/gateway_basic_esp32s3 -t upload        # 烧录 (USB CDC)
pio device monitor -d projects/gateway_basic_esp32s3       # 串口监视
pio run -d projects/gateway_basic_esp32s3 -t clean         # 清产物

# 完全清掉缓存
rm -rf projects/gateway_basic_esp32s3/.pio
```

### 产物位置

```
projects/gateway_basic_esp32s3/.pio/build/gateway_basic_esp32s3/
├── firmware.elf       # 调试符号
├── firmware.bin       # 烧录用
└── partitions.bin
```

### 烧录方式

- **首选**:USB CDC,无需按 BOOT 键,PIO 自动 reset
- **首次烧录卡住**:按住 BOOT + 短按 RESET 进入下载模式,再上传

### 不要做的事(PIO 侧)

- ❌ 在 gateway project 里跑 `west build` —— 没 west.yml
- ❌ `idf.py build` —— ESP-IDF 由 PIO 调度,不暴露原生入口
- ❌ 直接调 `cmake` / `ninja` —— PIO 自己管
- ❌ 改 `espressif32@6.7.0` 平台版本 —— 架构级,问人

---

## 已知踩坑

| 现象 | 原因 | 修复 |
|---|---|---|
| PIO + ESP-IDF: `fatal: not a git repository` at CMake configure | git_describe 失败 | 项目根 `CMakeLists.txt` 加 `set(PROJECT_VER "0.1.0")` 在 `include(...project.cmake)` 之前 |
| PIO + Zephyr: `ImportError: yaml` | PIO uv venv 没装 pyyaml | `uv tool install platformio --with pyyaml --with west --force` |
| PIO + Zephyr: `FileExistsError: zephyr/` | PIO 框架 bug,`os.makedirs` 不带 exist_ok | **不要用 PIO + Zephyr**,走 west(已是本架构默认) |
| west: 找不到 `ZEPHYR_BASE` | 没在 NCS toolchain 环境中 | `nrfutil toolchain-manager launch --` 包一层,或确认 `.west/` 存在 |
| west build 报老 API 找不到 | NCS 版本不对 | `cat zephyr/VERSION`,应是 3.7.x;否则 `west update` |
| XIAO ESP32-S3 串口不可见 | 板子无 USB-UART bridge IC | `sdkconfig.defaults` 加 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` |

---

## 完整命令矩阵(可贴 wiki)

| 任务 | Mote | Gateway |
|---|---|---|
| 安装工具链 | `nrfutil install toolchain-manager` + NCS | `brew install platformio` |
| 拉源码 | NCS 已在 `~/ncs/v2.9.2/`,无需 bootstrap | (PIO 按需拉) |
| 编译 | `ZEPHYR_BASE=~/ncs/v2.9.2/zephyr west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build_uf2` | `pio run -d projects/<name>` |
| 烧录 | `cp build_uf2/zephyr/zephyr.uf2 /Volumes/XIAO-SENSE/`(上电 5 秒内自动出现) | `pio run -d projects/<name> -t upload` |
| 监视 | `tio /dev/cu.usbmodem*`(USB CDC);`JLinkRTTViewer`(SWD) | `pio device monitor -d projects/<name>` |
| 清产物 | `west build -d build_uf2 -t pristine` | `rm -rf projects/<name>/.pio` |

---

## 端到端验证(mote ↔ gateway)

烧录两块板后,gateway 串口应看到 mote 的 BTHome motion 事件 JSON:

```
{"ts":8868528,"gw_id":"441bf6804166","mote_mac":"f0e3912cec19","rssi":-98,"moving":true,"vibration":false,"pid":169,"ctr":425}
{"ts":8872543,"gw_id":"441bf6804166","mote_mac":"f0e3912cec19","rssi":-98,"moving":true,"vibration":true,"pid":173,"ctr":429}
{"ts":8906822,"gw_id":"441bf6804166","mote_mac":"f0e3912cec19","rssi":-98,"moving":false,"vibration":false,"pid":207,"ctr":463}
```

判断好坏:

| 现象 | 结论 |
|---|---|
| 看到 `adv heard` 但没 JSON | BTHome Service Data 不匹配 v1 映射(查 `contracts/airframe.yaml`) |
| JSON `moving` / `vibration` 随动作变化 | mote 状态机和 BTHome payload 更新正常 |
| JSON `pid` / `ctr` 重复出现 | 正常:同一 payload 可能被重复广播;BTHome 接收端可用 `pid`,业务消费侧可用 `(mote_mac, ctr)` 去重 |
| 完全没有 `adv heard` | mote 没在广播,看 RTT(`JLinkRTTViewer`)确认 `adv started` |
