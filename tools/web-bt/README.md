# SeeedMote Web BT Config

Static HTTPS Web Bluetooth tool for field technicians. The hosted page only
serves HTML/JS; Chrome on the technician's phone or computer connects directly
to the mote's GATT service over local Bluetooth.

## Use

1. Open the hosted HTTPS page in Android Chrome or desktop Chrome.
2. Boot the mote or trigger one motion event.
3. Wait for the blue config-window blink.
4. Click `连接 SeeedMote`.
5. Keep the default **名称** scan mode and select the `SEEED-xxxxxx` device in
   Chrome's Bluetooth picker. If no `SEEED-` device appears but the mote is in
   the blue config window, switch to **兼容** mode and retry.
6. Read, save, restore defaults, or reboot.

## Scan modes

The page defaults to **名称** mode because dense Bluetooth environments make a
broad picker hard to use. It filters Chrome's picker to devices whose advertised
name starts with `SEEED-`, then verifies that the selected device exposes the
SeeedMote config GATT service.

Other modes are for fallback field debugging or future firmware variants:

| Mode | Browser filter | When to use |
|---|---|---|
| 名称 | `namePrefix: "SEEED-"` | Default, current firmware name format |
| Service | config service UUID | New firmware that advertises the UUID |
| 兼容 | `acceptAllDevices: true` | Old/unknown firmware when name filtering fails |
| 自定义 | custom `namePrefix` and/or service UUID | Temporary field variants |

Custom scan settings are saved in the technician's browser. They only affect
the next Web Bluetooth picker request; the page still connects to the fixed
SeeedMote config GATT service after a device is selected.

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
