# contracts/

Cross-device contracts (BLE airframe schema, gateway output JSON schema,
capability declarations). Both the mote firmware and every gateway parser
must match what is declared here, byte for byte.

## Status

| File | Status | Covers |
|---|---|---|
| [`airframe.yaml`](airframe.yaml) | v1 — uplink only, no auth | `mote_motion_*` → `gateway_*` 11-byte manufacturer-specific frame |

Downlink (config writes) will live in a separate `config-service.yaml` once
v2 GATT lands.

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

See `../docs/for-ai-agents.md` for the full operating manual.

## Versioning

`proto_version` in the airframe is bumped on any breaking layout change.
Gateways MUST drop frames whose `proto_version` they do not implement.
Adding a reserved-range enum value is NOT a breaking change.
