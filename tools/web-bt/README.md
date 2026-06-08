# SeeedMote Web BT Config

Static HTTPS Web Bluetooth tool for field technicians. The hosted page only
serves HTML/JS; Chrome on the technician's phone or computer connects directly
to the mote's GATT service over local Bluetooth.

## Use

1. Open the hosted HTTPS page in Android Chrome or desktop Chrome.
2. Boot the mote or trigger one motion event.
3. Wait for the blue config-window blink.
4. Click `连接 SeeedMote`.
5. Select the SeeedMote device in Chrome's Bluetooth picker.
6. Read, save, restore defaults, or reboot.

The sensitivity presets map to the same THS/DUR bytes exposed in the advanced
section:

| Preset | THS | DUR |
|---|---:|---:|
| 低 | `0x06` | `0x21` |
| 标准 | `0x03` | `0x21` |
| 高 | `0x02` | `0x21` |

Writes persist through reboot via Zephyr settings/NVS in the mote firmware.

## Deploy

This directory is deployed by `.github/workflows/web-bt-pages.yml` as a GitHub
Pages artifact. In GitHub, set:

`Settings` -> `Pages` -> `Build and deployment` -> `GitHub Actions`

Expected project-site URL:

`https://love4yzp.github.io/mote/`

If private-repository Pages is unavailable for the current GitHub plan, copy
only this directory to a small public repo such as `seeedmote-config` and enable
Pages there. Do not publish the full firmware/app repo just to share this tool.

Gateway does not handle downlink. This tool talks directly to the mote GATT
service.
