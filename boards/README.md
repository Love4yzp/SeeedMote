# boards/

Custom PlatformIO board JSON files for boards **not** in PIO's stock platform
packages. Currently empty.

## When to add a file here

- 你引入一块 PIO 没的板(如 XIAO ESP32-C6 在某些 PIO 版本里没收录、或自研板)
- 文件名:`<board_id>.json`,内容遵循 [PIO board manifest spec](https://docs.platformio.org/en/latest/platforms/creating_board.html)
- project 在 `platformio.ini` 里通过 `board = <board_id>` 引用,**并** 在 `[platformio]` 段加 `boards_dir = ../../boards`(看 `projects/gateway_basic_esp32s3/platformio.ini` 写法)

## Mote 侧(west + NCS)的板

**不需要在这里写**。XIAO BLE 在上游 Zephyr 是内置 board(`xiao_ble`),`west build -b xiao_ble` 直接识别。

## AI 注意

新建文件前先和用户确认 board ID 命名。**不要自己仿写一个 JSON**,容易在 `flash size` / `softdevice` / `upload protocol` 字段写错导致烧录砖板。
