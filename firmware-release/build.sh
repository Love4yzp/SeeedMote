#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
OUT_DIR="$SCRIPT_DIR"

MOTE_UF2="$ROOT_DIR/mote/build_uf2/zephyr/zephyr.uf2"
GW_FACTORY_BIN="$ROOT_DIR/gateway/.pio/build/seeedmote_gateway/firmware.factory.bin"

build_mote() {
    echo "=== Building mote (release) ==="
    "$ROOT_DIR/dev" mote build
    if [ ! -f "$MOTE_UF2" ]; then
        echo "ERROR: mote UF2 not found at $MOTE_UF2"
        return 1
    fi
    cp "$MOTE_UF2" "$OUT_DIR/mote.uf2"
    echo "  -> $(ls -lh "$OUT_DIR/mote.uf2" | awk '{print $5}')  mote.uf2"
}

build_gateway() {
    echo "=== Building gateway ==="
    "$ROOT_DIR/dev" gateway build
    if [ ! -f "$GW_FACTORY_BIN" ]; then
        echo "ERROR: gateway factory bin not found at $GW_FACTORY_BIN"
        return 1
    fi
    cp "$GW_FACTORY_BIN" "$OUT_DIR/gateway.factory.bin"
    echo "  -> $(ls -lh "$OUT_DIR/gateway.factory.bin" | awk '{print $5}')  gateway.factory.bin"
}

update_readme() {
    local commit_short commit_date mote_size gw_size
    commit_short=$(git -C "$ROOT_DIR" rev-parse --short HEAD)
    commit_date=$(date +%Y-%m-%d)
    mote_size=$(ls -lh "$OUT_DIR/mote.uf2" 2>/dev/null | awk '{print $5}' || echo "N/A")
    gw_size=$(ls -lh "$OUT_DIR/gateway.factory.bin" 2>/dev/null | awk '{print $5}' || echo "N/A")

    cat > "$OUT_DIR/README.md" << EOF
# SeeedMote v2 固件烧录指南

构建自 commit \`${commit_short}\` (${commit_date})

## 文件清单

| 文件 | 目标设备 | 大小 |
|------|---------|------|
| \`mote.uf2\` | XIAO nRF52840 Sense (Mote) | ~${mote_size} |
| \`gateway.factory.bin\` | XIAO ESP32-S3 (Gateway) | ~${gw_size} |

---

## 一、Mote 烧录（拖拽即可）

1. 用 USB-C 数据线连接 XIAO nRF52840 Sense 到电脑
2. **快速双击** 板上 RESET 按钮，进入 Bootloader 模式
   - 成功后电脑会弹出一个名为 **XIAO-SENSE** 的 U 盘
3. 把 \`mote.uf2\` 拖拽到 **XIAO-SENSE** U 盘中
4. 拷贝完成后 U 盘自动弹出，设备自动重启运行新固件

> 如果 U 盘没出现：检查数据线是否支持数据传输（不是纯充电线），再试双击 RESET。

---

## 二、Gateway 烧录

### 方法 A：用 esptool（推荐，无需安装 PlatformIO）

安装 esptool（如果没装过）：
\`\`\`bash
pip install esptool
\`\`\`

烧录：
\`\`\`bash
esptool --chip esp32s3 --baud 921600 write_flash 0x0 gateway.factory.bin
\`\`\`

> 如果串口找不到设备：按住 XIAO ESP32-S3 上的 **BOOT** 按钮，同时短按 **RESET**，然后松开 BOOT，进入下载模式。

### 方法 B：用 PlatformIO（如果已安装）

\`\`\`bash
cd gateway
pio run -t upload
\`\`\`

---

## 三、Gateway 首次配置

Gateway 上电后如果没有已保存的 WiFi 配置，会自动开启 AP 热点：

1. 用手机或电脑搜索 WiFi，连接名为 **seeedmote-gw-XXXXXX** 的热点
2. 打开浏览器访问 \`http://192.168.4.1\`
3. 在配置页面填入：
   - WiFi SSID 和密码
   - MQTT Broker 地址和端口
4. 点击保存，Gateway 会自动重启并连接

---

## 常见问题

**Q: Mote 烧录后没反应？**
A: Mote 固件是 release 模式，无 USB 串口输出。正常工作时 LED 会在 IMU 触发事件时短闪。如需调试日志，用 \`./dev mote flash --debug\` 重新编译带串口的版本。

**Q: Gateway 烧录时 esptool 找不到设备？**
A: 按住 BOOT → 短按 RESET → 松开 BOOT，让 ESP32-S3 进入下载模式后重试。

**Q: Gateway 连不上 MQTT？**
A: 通过 \`http://192.168.4.1\` 配置页检查 MQTT broker 地址和端口是否正确。
EOF
    echo "  -> README.md updated (commit ${commit_short}, ${commit_date})"
}

usage() {
    echo "Usage: $0 [all|mote|gateway]"
    echo ""
    echo "  all       Build both mote + gateway, update README (default)"
    echo "  mote      Build mote only"
    echo "  gateway   Build gateway only"
}

target="${1:-all}"
case "$target" in
    all)
        build_mote
        echo ""
        build_gateway
        echo ""
        update_readme
        echo ""
        echo "=== Done. Files in $OUT_DIR ==="
        ls -lh "$OUT_DIR"/*.uf2 "$OUT_DIR"/*.bin 2>/dev/null
        ;;
    mote)
        build_mote
        ;;
    gateway)
        build_gateway
        ;;
    -h|--help)
        usage
        ;;
    *)
        echo "Unknown target: $target"
        usage
        exit 1
        ;;
esac
