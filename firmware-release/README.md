# SeeedMote v2 固件烧录指南

构建自 commit `46bfbc8` (2026-06-11)

## 文件清单

| 文件 | 目标设备 | 大小 |
|------|---------|------|
| `mote.uf2` | XIAO nRF52840 Sense (Mote) | ~358 KB |
| `gateway.factory.bin` | XIAO ESP32-S3 (Gateway) | ~1.2 MB |

---

## 一、Mote 烧录（拖拽即可）

1. 用 USB-C 数据线连接 XIAO nRF52840 Sense 到电脑
2. **快速双击** 板上 RESET 按钮，进入 Bootloader 模式
   - 成功后电脑会弹出一个名为 **XIAO-SENSE** 的 U 盘
3. 把 `mote.uf2` 拖拽到 **XIAO-SENSE** U 盘中
4. 拷贝完成后 U 盘自动弹出，设备自动重启运行新固件

> 如果 U 盘没出现：检查数据线是否支持数据传输（不是纯充电线），再试双击 RESET。

---

## 二、Gateway 烧录

### 方法 A：用 esptool（推荐，无需安装 PlatformIO）

安装 esptool（如果没装过）：
```bash
pip install esptool
```

烧录：
```bash
esptool --chip esp32s3 --baud 921600 write_flash 0x0 gateway.factory.bin
```

> 如果串口找不到设备：按住 XIAO ESP32-S3 上的 **BOOT** 按钮，同时短按 **RESET**，然后松开 BOOT，进入下载模式。

### 方法 B：用 PlatformIO（如果已安装）

把 `gateway.factory.bin` 放回项目的 `gateway/.pio/build/seeedmote_gateway/` 目录，然后：
```bash
cd gateway
pio run -t upload
```

---

## 三、Gateway 首次配置

Gateway 上电后如果没有已保存的 WiFi 配置，会自动开启 AP 热点：

1. 用手机或电脑搜索 WiFi，连接名为 **seeedmote-gw-XXXXXX** 的热点
2. 打开浏览器访问 `http://192.168.4.1`
3. 在配置页面填入：
   - WiFi SSID 和密码
   - MQTT Broker 地址和端口
4. 点击保存，Gateway 会自动重启并连接

---

## 常见问题

**Q: Mote 烧录后没反应？**
A: Mote 固件是 release 模式，无 USB 串口输出。正常工作时 LED 会在 IMU 触发事件时短闪。如需调试日志，用 `./dev mote flash --debug` 重新编译带串口的版本。

**Q: Gateway 烧录时 esptool 找不到设备？**
A: 按住 BOOT → 短按 RESET → 松开 BOOT，让 ESP32-S3 进入下载模式后重试。

**Q: Gateway 连不上 MQTT？**
A: 通过 `http://192.168.4.1` 配置页检查 MQTT broker 地址和端口是否正确。
