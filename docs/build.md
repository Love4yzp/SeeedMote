# Build — 工具链与命令清单

主入口是仓库根目录的 `./dev`。根 `Makefile` 只保留兼容转发,不要把它当成人用入口。

工具链边界:

```
mote/      → west + nRF Connect SDK (NCS v2.9.2)
gateway/   → ESPHome YAML
app/       → FastAPI + React demo
```

Mote 是 west + NCS;Gateway 是 ESPHome YAML。**不要让 AI 自己选轨**。

---

## 一次性检查

```bash
./dev doctor
```

`doctor` 会检查 `nrfutil`、`tio`、`esphome`、测试默认 `gateway/secrets.yaml`、NCS 路径、UF2 卷和当前串口列表。

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

NCS v2.9.2 安装在 `~/ncs/v2.9.2/`(由 nrfutil toolchain-manager 管理,共享,不在 project 目录里)。

编译时 `./dev` 通过 `--ncs` 派生 `ZEPHYR_BASE`,**无需在 project 目录下跑 `west init` / `west update`**。

### 编译 / 烧录 / 监视

```bash
./dev mote build
./dev mote build --debug
./dev mote build --clean

./dev mote flash
./dev mote flash --debug
./dev mote flash --volume /Volumes/XIAO-SENSE

./dev mote log
./dev mote log --port /dev/cu.usbmodem101
./dev mote run --debug

./dev mote clean
./dev mote build --ncs v2.9.2
```

`./dev mote flash` 会先构建 UF2,然后尝试 1200-baud touch 进入 bootloader。Release 固件没有 USB CDC 时,按提示双击 XIAO RESET,等待 `/Volumes/XIAO-SENSE` 出现。

### 直接调用 west

仅当 `./dev` 不够用时才直接调用 west:

```bash
cd mote
ZEPHYR_BASE=~/ncs/v2.9.2/zephyr nrfutil toolchain-manager launch --ncs-version v2.9.2 -- \
  west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build_uf2
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
${EDITOR:-vi} gateway/secrets.yaml                         # 可选:修改测试 WiFi/MQTT 默认值
```

### 编译 / 烧录 / 监视

```bash
./dev gateway compile
./dev gateway run
./dev gateway log
```

等价底层命令:

```bash
esphome compile gateway/esphome.yaml
esphome run gateway/esphome.yaml
esphome logs gateway/esphome.yaml
```

Gateway 使用统一固件。`gateway/esphome.yaml` 通过 `name_add_mac_suffix: true` 自动把 MAC 后缀追加到 ESPHome node name,所以 MQTT payload 里的 `gw` 会是类似 `seeedmote-gw-a1b2c3` 的稳定 ID。用户可读位置名在消费侧 `app/gateways.yaml` 配 alias,不需要重新烧录。

测试阶段 `gateway/secrets.yaml` 已带默认 WiFi/MQTT 值。OTA 不设密码,初始化 fallback AP 也是开放 AP。MQTT username 在启动时自动设为带 MAC 后缀的 gateway ID(例如 `seeedmote-gw-a1b2c3`),不在 secrets 中手填。

### 烧录方式

- **首选**:USB CDC,ESPHome 自动 reset
- **首次烧录卡住**:按住 BOOT + 短按 RESET 进入下载模式,再运行 `./dev gateway run`

### 不要做的事(Gateway 侧)

- ❌ 在 `gateway/` 写 C / CMake / PlatformIO
- ❌ 在 gateway 里跑 `west build`
- ❌ 加 BLE Client 做下行
- ❌ 使用 `bthome_receiver` 外部组件

---

## App demo

```bash
./dev app run
./dev app run --mock
./dev app install
```

---

## 已知踩坑

| 现象 | 原因 | 修复 |
|---|---|---|
| PIO + ESP-IDF: `fatal: not a git repository` at CMake configure | git_describe 失败 | 项目根 `CMakeLists.txt` 加 `set(PROJECT_VER "0.1.0")` 在 `include(...project.cmake)` 之前 |
| PIO + Zephyr: `ImportError: yaml` | PIO uv venv 没装 pyyaml | `uv tool install platformio --with pyyaml --with west --force` |
| PIO + Zephyr: `FileExistsError: zephyr/` | PIO 框架 bug,`os.makedirs` 不带 exist_ok | **不要用 PIO + Zephyr**,走 west(已是本架构默认) |
| west: 找不到 `ZEPHYR_BASE` | 没在 NCS toolchain 环境中 | 用 `./dev` 或显式包 `nrfutil toolchain-manager launch --` |
| west build 报老 API 找不到 | NCS 版本不对 | 确认使用 `./dev mote build --ncs v2.9.2` |
| XIAO ESP32-S3 串口不可见 | 板子无 USB-UART bridge IC | ESPHome 配置应启用 USB Serial/JTAG console |

---

## 完整命令矩阵

| 任务 | Mote | Gateway |
|---|---|---|
| 安装工具链 | `nrfutil install toolchain-manager` + NCS | `pip install esphome` |
| 检查环境 | `./dev doctor` | `./dev doctor` |
| 编译 | `./dev mote build` | `./dev gateway compile` |
| 烧录 | `./dev mote flash` | `./dev gateway run` |
| 监视 | `./dev mote log`(DEBUG 固件走 USB CDC);`JLinkRTTViewer`(RTT/SWD) | `./dev gateway log` |
| 清产物 | `./dev mote clean` | 删除 ESPHome 本地 build 缓存 |

---

## 端到端验证(mote ↔ gateway)

烧录两块板后,MQTT broker 应看到 gateway 发布的 SeeedMote 事件:

```
seeedmote/f0e3912cec19/online {"rssi":-74,"gw":"seeedmote-gw-a1b2c3"}
seeedmote/f0e3912cec19/event  {"packet_id":42,"rssi":-68,"gw":"seeedmote-gw-a1b2c3"}
```

判断好坏:

| 现象 | 结论 |
|---|---|
| 看到 BLE adv 但没 MQTT | BTHome Service Data 不含 `packet_id` + `moving`,查 `AGENTS.md §5` |
| `/online` 在 boot 后出现 | boot heartbeat 正常 |
| `/event` 随动作出现 | IMU WAKE_UP + BTHome payload 正常 |
| 同一 `packet_id` 被多个 gateway 上报 | 正常:consumer 用 `(mote_mac, packet_id)` 去重 |
| 完全没有 BLE adv | mote 静默可能正常;boot 或摇动后再看 RTT(`JLinkRTTViewer`) |
