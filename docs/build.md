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

### Bootstrap 一个新 mote project(一次性,~4 GB)

```bash
cd projects/mote_motion_nrf52840
west init -l .
west update
```

### 编译 / 烧录 / 监视

```bash
cd projects/mote_motion_nrf52840

# 编译
west build -b xiao_ble

# 清编译
west build -t pristine

# 烧录:首选 UF2 拖拽
cp build/zephyr/zephyr.uf2 /Volumes/XIAOBOOT/
# 进 XIAOBOOT 模式:双击 XIAO 板上的 RESET 按钮

# 备选:SWD(需要 J-Link / CMSIS-DAP)
west flash

# RTT 日志(SWD 探针)
JLinkRTTViewer
# 或 J-Link Commander + RTT telnet
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

---

## 完整命令矩阵(可贴 wiki)

| 任务 | Mote | Gateway |
|---|---|---|
| 安装工具链 | `nrfutil install toolchain-manager` + NCS | `brew install platformio` |
| 拉源码 | `west init -l . && west update` | (PIO 按需拉) |
| 编译 | `west build -b xiao_ble` | `pio run -d projects/<name>` |
| 烧录 | `cp build/.../*.uf2 /Volumes/XIAOBOOT/` | `pio run -d projects/<name> -t upload` |
| 监视 | `JLinkRTTViewer` 或 `tio /dev/cu.usbmodem*` | `pio device monitor -d projects/<name>` |
| 清产物 | `west build -t pristine` | `rm -rf projects/<name>/.pio` |
