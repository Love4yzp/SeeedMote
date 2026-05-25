# contracts/

Cross-device contracts (BLE airframe + MQTT topics + payloads). Mote
firmware, gateway parsers, and consumer applications must match what is
declared here.

The system is **event-driven**: contracts describe discrete events, never
periodic telemetry. If you find yourself adding a heartbeat or counter
stream to a contract, you are on the wrong path — see `airframe.yaml`
top comment for the model.

## Status

| File | Status | Covers |
|---|---|---|
| [`airframe.yaml`](airframe.yaml) | v1 — BTHome v2, uplink only, no auth | `mote_motion_*` → `gateway_*` BLE Service Data |
| [`mqtt-uplink.yaml`](mqtt-uplink.yaml) | v1 — event + status, no telemetry | `gateway_*` → broker → consumer (event, status) |
| [`mqtt-downlink.yaml`](mqtt-downlink.yaml) | v1 — JSON-only cmd, no bare strings | consumer → broker → `gateway_*` (cmd) |

Connection-oriented mote configuration (if ever needed) will land in a
separate `config-service.yaml`. The current MQTT downlink targets the
gateway, not the mote.

## How to change a contract

A contract change is a **cross-device change**. Adding or renaming a field
requires synchronised edits in:

1. the contract file in this directory
2. every mote project that emits the frame
3. every gateway project that decodes it

This is a human-driven coordination. **AI agents must not edit files here
without explicit human instruction** and must not land a contract change in
the same commit as a downstream code change — split into:

- `contract:` — change the schema
- `mote:` / `gateway:` — implement on each side, referencing the contract version

See the root `AGENTS.md` for the full operating manual.

## Versioning

`contract.version` is bumped on any breaking SeeedMote mapping change.
BTHome object ids and their standard semantics remain governed by BTHome;
adding a higher-numbered optional object is usually non-breaking because
BTHome receivers parse object ids in ascending order and stop at unknown ids.
