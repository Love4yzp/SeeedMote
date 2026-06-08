# SeeedMote App Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the retail app replacement so the current SeeedMote v2 app is usable for real booth interaction, device configuration, and repeatable local verification.

**Architecture:** Keep the app as a consumer-side reference implementation: FastAPI owns MQTT ingestion, WebSocket fanout, static serving, and runtime MQTT settings; React owns display-only retail interaction UI. Add the missing Web Bluetooth configuration tool as a separate static surface under `tools/web-bt/`, because the product contract says mote downlink configuration is direct Web BT and gateway remains a pure MQTT pipe.

**Tech Stack:** FastAPI, paho-mqtt, React 18, Vite, Zustand, TypeScript, Web Bluetooth API, Zephyr/NCS mote GATT contract, ESPHome gateway MQTT contract.

---

## File Structure

- Modify `app/shoes.yaml`: register the real mote MAC observed in current app traffic.
- Modify `app/README.md`: document how real mote registration works and how to verify unknown-device fallback.
- Modify `app/backend/settings.py`: allow the backend port to be overridden with `SEEEDMOTE_APP_PORT` or `PORT`.
- Modify `app/app/vite.config.ts`: allow Vite dev port and backend target to be overridden with env variables.
- Modify `app/Makefile`: pick matching backend/frontend ports and pass the backend URL to Vite.
- Modify `dev`: keep `./dev app run --mock` as the user-facing entrypoint while forwarding port options through the app Makefile.
- Create `tools/web-bt/index.html`: static Chrome/Android Web Bluetooth configuration page for THS, DUR, and reboot.
- Create `tools/web-bt/README.md`: usage, browser limits, and relationship to the 30s config window.
- Modify root `README.md`: keep repo layout truthful and link to the implemented Web BT tool.
- Add `app/backend/test_settings.py`: prove env aliases for backend port work.
- Add `app/app/src/__tests__` only if a frontend test runner is introduced. Do not add a frontend test framework just for this plan; use TypeScript build plus Playwright smoke instead.

## Task 1: Register Current Real Mote in Retail Catalog

**Files:**
- Modify: `app/shoes.yaml`
- Modify: `app/README.md`

- [ ] **Step 1: Confirm the full real mote MAC**

Run:

```bash
curl -sS http://127.0.0.1:3001/ws
```

Expected: This will not work because `/ws` is a WebSocket, so use the UI diagnostics instead.

Open the app, expand the first event's `查看` details, and record:

```text
mote_mac: <full lowercase no-colon mac ending in 8224d6>
packet_id: <number>
rssi: <number>
gateway: <gw id>
```

- [ ] **Step 2: Add the real mote metadata**

Edit `app/shoes.yaml` and add the observed MAC under `shoes:`. Use placeholder retail metadata only if the actual SKU is not known, but keep it visibly editable:

```yaml
  "<full_mac_ending_8224d6>":
    sku: "FIELD-001"
    name: "现场样品鞋"
    color: "待登记"
    price: 0
    image: "assets/sh001.svg"
```

- [ ] **Step 3: Document the real registration path**

Append this under `app/README.md` section `Register a new mote`:

```markdown
For field setup, first run the app, expand `互动记录 -> 诊断 -> 查看`, copy the full `mote_mac`, then add it to `app/shoes.yaml`. Restart the backend after editing because the catalog is loaded during FastAPI lifespan startup.
```

- [ ] **Step 4: Verify catalog behavior**

Run:

```bash
python3 -m unittest test_semantic_events.py
npm run build
```

Expected:

```text
Ran 3 tests
OK
✓ built
```

- [ ] **Step 5: Browser smoke**

Run the app with the real backend and trigger one mote event.

Expected UI:

```text
互动记录 shows 已登记商品
正在互动 card shows FIELD-001 or the final SKU
累计互动 row no longer says 未登记 for that MAC
```

## Task 2: Make App Dev Ports Explicit and Re-runnable

**Files:**
- Modify: `app/backend/settings.py`
- Create: `app/backend/test_settings.py`
- Modify: `app/app/vite.config.ts`
- Modify: `app/Makefile`
- Modify: `dev`

- [ ] **Step 1: Add failing backend settings tests**

Create `app/backend/test_settings.py`:

```python
import os
import unittest

from settings import Settings


class SettingsTests(unittest.TestCase):
    def tearDown(self):
        os.environ.pop("SEEEDMOTE_APP_PORT", None)
        os.environ.pop("PORT", None)

    def test_port_accepts_seeedmote_app_port(self):
        os.environ["SEEEDMOTE_APP_PORT"] = "3101"
        settings = Settings()
        self.assertEqual(settings.port, 3101)

    def test_port_accepts_port_alias(self):
        os.environ["PORT"] = "3201"
        settings = Settings()
        self.assertEqual(settings.port, 3201)


if __name__ == "__main__":
    unittest.main()
```

Run:

```bash
cd app/backend && python3 -m unittest test_settings.py
```

Expected before implementation: FAIL because `port` has no env alias.

- [ ] **Step 2: Implement backend port aliases**

In `app/backend/settings.py`, replace:

```python
    port: int = 3001
```

with:

```python
    port: int = Field(
        default=3001,
        validation_alias=AliasChoices("SEEEDMOTE_APP_PORT", "PORT"),
    )
```

- [ ] **Step 3: Run backend tests**

Run:

```bash
cd app/backend && python3 -m unittest test_settings.py test_semantic_events.py
```

Expected:

```text
Ran 5 tests
OK
```

- [ ] **Step 4: Make Vite backend target configurable**

In `app/app/vite.config.ts`, replace the current `server` block with:

```ts
const frontendPort = Number(process.env.SEEEDMOTE_FRONTEND_PORT ?? 5173);
const backendTarget = process.env.SEEEDMOTE_BACKEND_URL ?? 'http://localhost:3001';
const backendWsTarget = backendTarget.replace(/^http:/, 'ws:').replace(/^https:/, 'wss:');

export default defineConfig({
  plugins: [react()],
  server: {
    port: frontendPort,
    strictPort: false,
    proxy: {
      '/api': backendTarget,
      '/assets': backendTarget,
      '/ws': { target: backendWsTarget, ws: true },
    },
  },
  build: {
    outDir: 'dist',
  },
});
```

- [ ] **Step 5: Pass ports from app Makefile**

In `app/Makefile`, add:

```make
BACKEND_PORT ?= 3001
FRONTEND_PORT ?= 5173
BACKEND_URL := http://localhost:$(BACKEND_PORT)
```

Then replace the `dev` recipe command with:

```make
	@trap 'kill 0' INT TERM; \
	  MOCK=$(MOCK) SEEEDMOTE_APP_PORT=$(BACKEND_PORT) $(PYTHON) $(BACKEND_DIR)/main.py & \
	  SEEEDMOTE_FRONTEND_PORT=$(FRONTEND_PORT) SEEEDMOTE_BACKEND_URL=$(BACKEND_URL) npm --prefix $(FRONTEND_DIR) run dev & \
	  wait
```

- [ ] **Step 6: Add CLI pass-through options**

In `dev`, add arguments to `app_run_parser`:

```python
    app_run_parser.add_argument("--backend-port", default="3001", help="Backend port, default: 3001")
    app_run_parser.add_argument("--frontend-port", default="5173", help="Frontend port, default: 5173")
```

Then update `app_command` so target `dev` appends:

```python
        command.append(f"BACKEND_PORT={args.backend_port}")
        command.append(f"FRONTEND_PORT={args.frontend_port}")
```

- [ ] **Step 7: Verify port-conflict recovery**

Run with alternate ports:

```bash
./dev app run --mock --backend-port 3101 --frontend-port 5175
```

Expected:

```text
Started server process
Application startup complete
Local: http://localhost:5175/
```

Open `http://localhost:5175/`.

Expected UI:

```text
Mock source
MOCK / WS
Connected
互动记录 receives scripted events
```

## Task 3: Add Web Bluetooth Configuration Tool

**Files:**
- Create: `tools/web-bt/index.html`
- Create: `tools/web-bt/README.md`
- Modify: `README.md`

- [ ] **Step 1: Confirm mote GATT constants**

Read `mote/src/cfg_svc.c` and identify the actual service UUID and characteristic UUIDs for:

```text
wake threshold THS
wake duration DUR
factory reboot/reset command
```

Do not invent UUIDs. Copy the exact constants from the source file.

- [ ] **Step 2: Create the static Web BT page**

Create `tools/web-bt/index.html` with:

```html
<!doctype html>
<html lang="zh-CN">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>SeeedMote Web BT Config</title>
    <style>
      :root {
        color-scheme: light;
        font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
        background: #f8fafc;
        color: #0f172a;
      }
      body {
        margin: 0;
        min-height: 100vh;
      }
      main {
        max-width: 760px;
        margin: 0 auto;
        padding: 32px 20px;
      }
      header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 16px;
        margin-bottom: 24px;
      }
      h1 {
        margin: 0;
        font-size: 24px;
        line-height: 1.2;
      }
      .panel {
        background: #fff;
        border: 1px solid #e2e8f0;
        border-radius: 8px;
        padding: 20px;
        margin-bottom: 16px;
      }
      .status {
        font-size: 13px;
        color: #475569;
      }
      .grid {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
        gap: 16px;
      }
      label {
        display: block;
        font-size: 12px;
        font-weight: 700;
        color: #475569;
        text-transform: uppercase;
        letter-spacing: 0.04em;
        margin-bottom: 6px;
      }
      input {
        width: 100%;
        box-sizing: border-box;
        border: 1px solid #cbd5e1;
        border-radius: 6px;
        padding: 10px 12px;
        font-size: 16px;
      }
      button {
        border: 0;
        border-radius: 6px;
        padding: 10px 14px;
        font-weight: 700;
        cursor: pointer;
      }
      button.primary {
        background: #0fae3c;
        color: #fff;
      }
      button.secondary {
        background: #e2e8f0;
        color: #0f172a;
      }
      button.danger {
        background: #dc2626;
        color: #fff;
      }
      button:disabled {
        opacity: 0.45;
        cursor: not-allowed;
      }
      .actions {
        display: flex;
        flex-wrap: wrap;
        gap: 10px;
        margin-top: 16px;
      }
      pre {
        white-space: pre-wrap;
        background: #0f172a;
        color: #dbeafe;
        border-radius: 8px;
        padding: 12px;
        min-height: 120px;
        font-size: 12px;
      }
    </style>
  </head>
  <body>
    <main>
      <header>
        <div>
          <h1>SeeedMote Config</h1>
          <div class="status" id="support"></div>
        </div>
        <button class="primary" id="connect">Connect</button>
      </header>

      <section class="panel">
        <div class="grid">
          <div>
            <label for="ths">Wake threshold THS</label>
            <input id="ths" type="number" min="0" max="63" step="1" />
          </div>
          <div>
            <label for="dur">Wake duration DUR</label>
            <input id="dur" type="number" min="0" max="255" step="1" />
          </div>
        </div>
        <div class="actions">
          <button class="secondary" id="read" disabled>Read</button>
          <button class="primary" id="write" disabled>Write</button>
          <button class="danger" id="reboot" disabled>Reboot</button>
        </div>
      </section>

      <section class="panel">
        <label>Log</label>
        <pre id="log"></pre>
      </section>
    </main>

    <script type="module">
      const UUIDS = {
        service: "a8b00001-3e8e-4b8f-9a1c-9b1f5e88aa00",
        ths: "a8b00002-3e8e-4b8f-9a1c-9b1f5e88aa00",
        dur: "a8b00003-3e8e-4b8f-9a1c-9b1f5e88aa00",
        reboot: "a8b00004-3e8e-4b8f-9a1c-9b1f5e88aa00",
      };

      const state = {
        device: null,
        server: null,
        service: null,
        chars: {},
      };

      const els = {
        support: document.querySelector("#support"),
        connect: document.querySelector("#connect"),
        read: document.querySelector("#read"),
        write: document.querySelector("#write"),
        reboot: document.querySelector("#reboot"),
        ths: document.querySelector("#ths"),
        dur: document.querySelector("#dur"),
        log: document.querySelector("#log"),
      };

      function log(message) {
        const ts = new Date().toLocaleTimeString("zh-CN", { hour12: false });
        els.log.textContent = `[${ts}] ${message}\n${els.log.textContent}`;
      }

      function setConnected(connected) {
        els.read.disabled = !connected;
        els.write.disabled = !connected;
        els.reboot.disabled = !connected;
        els.connect.textContent = connected ? "Reconnect" : "Connect";
      }

      async function connect() {
        if (!navigator.bluetooth) {
          log("Web Bluetooth is not available. Use Chrome on Android or desktop Chrome.");
          return;
        }

        state.device = await navigator.bluetooth.requestDevice({
          filters: [{ services: [UUIDS.service] }],
          optionalServices: [UUIDS.service],
        });
        state.device.addEventListener("gattserverdisconnected", () => {
          setConnected(false);
          log("Disconnected");
        });
        state.server = await state.device.gatt.connect();
        state.service = await state.server.getPrimaryService(UUIDS.service);
        state.chars.ths = await state.service.getCharacteristic(UUIDS.ths);
        state.chars.dur = await state.service.getCharacteristic(UUIDS.dur);
        state.chars.reboot = await state.service.getCharacteristic(UUIDS.reboot);
        setConnected(true);
        log(`Connected to ${state.device.name || state.device.id}`);
        await readValues();
      }

      async function readU8(characteristic) {
        const value = await characteristic.readValue();
        return value.getUint8(0);
      }

      async function writeU8(characteristic, value) {
        const payload = new Uint8Array([value]);
        await characteristic.writeValue(payload);
      }

      function parseU8(input, label, max) {
        const value = Number(input.value);
        if (!Number.isInteger(value) || value < 0 || value > max) {
          throw new Error(`${label} must be an integer from 0 to ${max}`);
        }
        return value;
      }

      async function readValues() {
        els.ths.value = String(await readU8(state.chars.ths));
        els.dur.value = String(await readU8(state.chars.dur));
        log(`Read THS=${els.ths.value}, DUR=${els.dur.value}`);
      }

      async function writeValues() {
        const ths = parseU8(els.ths, "THS", 63);
        const dur = parseU8(els.dur, "DUR", 255);
        await writeU8(state.chars.ths, ths);
        await writeU8(state.chars.dur, dur);
        log(`Wrote THS=${ths}, DUR=${dur}`);
      }

      async function reboot() {
        if (!confirm("Reboot this mote now?")) return;
        await writeU8(state.chars.reboot, 1);
        log("Reboot command sent");
      }

      els.support.textContent = navigator.bluetooth
        ? "Chrome / Android, connect during the 30s config window"
        : "Web Bluetooth unavailable in this browser";
      els.connect.addEventListener("click", () => connect().catch((err) => log(err.message)));
      els.read.addEventListener("click", () => readValues().catch((err) => log(err.message)));
      els.write.addEventListener("click", () => writeValues().catch((err) => log(err.message)));
      els.reboot.addEventListener("click", () => reboot().catch((err) => log(err.message)));
    </script>
  </body>
</html>
```

These UUIDs come from `mote/src/cfg_svc.c`: service `a8b00001-...`, THS `a8b00002-...`, DUR `a8b00003-...`, CMD `a8b00004-...`.

- [ ] **Step 3: Create tool README**

Create `tools/web-bt/README.md`:

```markdown
# SeeedMote Web BT Config

Static Chrome / Android Web Bluetooth tool for configuring mote IMU wake-up tuning during the 30s connectable window opened after boot or motion.

## Use

1. Boot the mote or trigger a motion event.
2. Open `tools/web-bt/index.html` in Chrome.
3. Click `Connect`.
4. Select the SeeedMote device.
5. Read or write THS/DUR.
6. Use `Reboot` only when you intentionally want the mote to restart.

Gateway does not handle downlink. This tool talks directly to the mote GATT service.
```

- [ ] **Step 4: Make root README truthful**

In `README.md`, keep the `tools/web-bt/` layout entry and add:

```markdown
`tools/web-bt/` is the Chrome / Android direct configuration tool for the mote GATT service. It is intentionally separate from `app/` because gateway downlink over MQTT is not part of v2.
```

- [ ] **Step 5: Static validation**

Run:

```bash
rg -n "PASTE_|TODO|FIXME" tools/web-bt
```

Expected: no output.

Open `tools/web-bt/index.html` in Chrome.

Expected:

```text
Page renders
Unsupported browsers show Web Bluetooth unavailable
Chrome shows Connect button enabled
```

## Task 4: Production Static Serving and Verification Contract

**Files:**
- Modify: `app/README.md`
- Modify: `app/backend/main.py` only if live static serving remains confusing after Task 2.

- [ ] **Step 1: Document static serving lifecycle**

Add to `app/README.md`:

Add this text:

```text
For production-style serving, build the frontend first.

Run:
cd app/app && npm run build
cd ../backend && python3 main.py

FastAPI mounts `app/app/dist` at startup if the directory exists. If you build after the backend is already running, restart the backend.
```

- [ ] **Step 2: Verify static serving from a fresh backend**

Run:

```bash
cd app/app && npm run build
cd ../backend && SEEEDMOTE_APP_PORT=3102 MOCK=true python3 main.py
```

Open:

```text
http://localhost:3102/
```

Expected:

```text
SeeedMote Retail page renders without Vite
Mock source appears
事件流 receives scripted events
```

- [ ] **Step 3: Decide if code change is needed**

If the fresh backend serves the built app, do not modify `app/backend/main.py`.

If it still returns `{"detail":"Not Found"}`, change the mount block to log the resolved dist path and fail loudly in app startup when `SERVE_FRONTEND_REQUIRED=true` is set. Do not make frontend serving mandatory by default.

## Task 5: Final Verification

**Files:**
- No new implementation files.

- [ ] **Step 1: Python tests**

Run:

```bash
cd app/backend && python3 -m unittest test_settings.py test_semantic_events.py
```

Expected:

```text
Ran 5 tests
OK
```

- [ ] **Step 2: Frontend build**

Run:

```bash
cd app/app && npm run build
```

Expected:

```text
tsc && vite build
✓ built
```

- [ ] **Step 3: Dev mock smoke**

Run:

```bash
./dev app run --mock --backend-port 3101 --frontend-port 5175
```

Expected:

```text
Backend starts on 3101
Vite starts on 5175
http://localhost:5175/ shows Mock source and Connected
```

- [ ] **Step 4: Real MQTT smoke**

Run:

```bash
./dev app run --backend-port 3001 --frontend-port 5173
```

Trigger a real mote event.

Expected:

```text
Header shows MQTT / WS Connected
网关在线 is at least 1/1
互动记录 gets one 商品被拿起 row
Registered mote row shows 已登记商品
Unknown mote row still shows 未登记设备
```

- [ ] **Step 5: Web BT smoke**

Open `tools/web-bt/index.html` in Chrome during a mote config window.

Expected:

```text
Connect succeeds
Read returns current THS and DUR
Write updates THS and DUR
Reboot sends command only after intentional click
```

## Self-Review

- Spec coverage: The plan covers the three observed gaps: real mote registration, missing Web BT tool, and mock/dev verification under port conflicts. It also documents production static serving behavior.
- Placeholder scan: The Web BT page includes concrete UUIDs from `mote/src/cfg_svc.c`; Task 3 Step 5 verifies no placeholder markers remain.
- Type consistency: Backend setting names are `SEEEDMOTE_APP_PORT` / `PORT`; Vite variables are `SEEEDMOTE_FRONTEND_PORT` / `SEEEDMOTE_BACKEND_URL`; CLI flags are `--backend-port` / `--frontend-port`.
