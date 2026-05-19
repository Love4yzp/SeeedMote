# contracts/

This directory is **reserved** for cross-device contracts (BLE airframe schema,
gateway output JSON schema, capability declarations).

**Status (this init): empty on purpose.** Real schemas land here only after at
least one mote project and one gateway project have working business code that
needs to talk to each other. Until then, the contract is "there is no contract."

**AI agents must not create files here without explicit human instruction.** A
contract change is a cross-device change — adding a field requires synchronised
edits in both a mote project and a gateway project, and that coordination is a
human decision, not an AI one.

See `../docs/for-ai-agents.md` for the full operating manual.
