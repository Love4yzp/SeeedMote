# Build — 工具链与命令清单

**双轨制**:**工具链由芯片家族决定**,不由 role 决定:

```
projects/<any role>_*_nrf52*/     →  west + nRF Connect SDK (NCS v2.9.2)
gateway/                           →  ESPHome YAML
projects/<any role>_*_rp2040/     →  未定,问人
```

Mote 是 west + NCS;Gateway 是 ESPHome YAML。**不要让 AI 自己选轨**。

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

## Gateway 侧 · ESPHome YAML

### 一次性安装

```bash
pip install esphome
esphome version
```

### 编译 / 烧录 / 监视

```bash
# 从仓库根
cp gateway/secrets.yaml.example gateway/secrets.yaml       # 填 WiFi/MQTT
esphome compile gateway/esphome.yaml                       # 编译
esphome run gateway/esphome.yaml                           # 编译 + 烧录 + 日志
esphome logs gateway/esphome.yaml                          # 只看日志
```

### 烧录方式

- **首选**:USB CDC,ESPHome 自动 reset
- **首次烧录卡住**:按住 BOOT + 短按 RESET 进入下载模式,再运行 `esphome run`

### 不要做的事(Gateway 侧)

- ❌ 在 `gateway/` 写 C / CMake / PlatformIO
- ❌ 在 gateway 里跑 `west build`
- ❌ 加 BLE Client 做下行
- ❌ 使用 `bthome_receiver` 外部组件

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
| 安装工具链 | `nrfutil install toolchain-manager` + NCS | `pip install esphome` |
| 拉源码 | NCS 已在 `~/ncs/v2.9.2/`,无需 bootstrap | ESPHome 按需拉平台包 |
| 编译 | `make build` | `esphome compile gateway/esphome.yaml` |
| 烧录 | `make flash` | `esphome run gateway/esphome.yaml` |
| 监视 | `make monitor`(DEBUG=1 USB CDC);`JLinkRTTViewer`(RTT/SWD) | `esphome logs gateway/esphome.yaml` |
| 清产物 | `make clean` | 删除 ESPHome 本地 build 缓存 |

---

## 端到端验证(mote ↔ gateway)

烧录两块板后,MQTT broker 应看到 gateway 发布的 SeeedMote 事件:

```
seeedmote/f0e3912cec19/online {"rssi":-74,"gw":"seeedmote-gateway"}
seeedmote/f0e3912cec19/event  {"packet_id":42,"rssi":-68,"gw":"seeedmote-gateway"}
```

判断好坏:

| 现象 | 结论 |
|---|---|
| 看到 BLE adv 但没 MQTT | BTHome Service Data 不含 `packet_id` + `moving`,查 `AGENTS.md §5` |
| `/online` 在 boot 后出现 | boot heartbeat 正常 |
| `/event` 随动作出现 | IMU WAKE_UP + BTHome payload 正常 |
| 同一 `packet_id` 被多个 gateway 上报 | 正常:consumer 用 `(mote_mac, packet_id)` 去重 |
| 完全没有 BLE adv | mote 静默可能正常;boot 或摇动后再看 RTT(`JLinkRTTViewer`) |
