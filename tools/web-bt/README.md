# SeeedMote Web BT Config

Static Chrome / Android Web Bluetooth tool for configuring mote IMU wake-up
tuning during the 30s connectable window opened after boot or motion.

## Use

1. Boot the mote or trigger a motion event.
2. Open `tools/web-bt/index.html` in Chrome.
3. Click `Connect`.
4. Select the SeeedMote device.
5. Read or write THS/DUR.
6. Use `Reboot` only when you intentionally want the mote to restart.

Gateway does not handle downlink. This tool talks directly to the mote GATT
service.
