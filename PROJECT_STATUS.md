# Project Status

**Status:** Phases 0 and 1 are implemented. Phase 2 has a live native M5Tab5 display/touch and authenticated direct-USB resilience proof; Wi-Fi/TLS and production key protection remain open.

## Created

- Initial architecture, capability, adapter, security, scope, roadmap, decision, and open-question documents.
- First-class M5Tab5 edge-node and operator-console design.
- Example M5Tab5 node and capability records.
- Minimum hardware-neutral node, event, capability, command, and adapter-envelope contracts for the simulator.
- Minimal Rust protocol crate that loads the simulator JSONL fixture and checks cross-record admission invariants.
- Deterministic online, offline, slow, and failing node simulator with policy denial, timeout, cancellation, verification, idempotency, and restart reconciliation checks.
- Real local Windows host and Ollama sidecars emit protocol-v1 hello and node snapshot records without cloud access.
- Windows service mutations are default-deny behind an explicit allowlist; Ollama generation is installed-model-only and capped at 128 output tokens.
- Live local checks exercised Windows service status, bounded Ollama generation, and immediate generation cancellation.
- Minimal `orclinkd` owns adapter collection, persists records in SQLite, and serves loopback read APIs for snapshots, operations, and timeline.
- Minimal Tauri Control Room reads daemon-owned node snapshots, operations, and timeline with visible online/offline state.
- M5Tab5 firmware boots on the measured unit, renders a 1280x720 touch UI, reports structured telemetry, and preserves its stable node identity.
- Physical-USB pairing provisions one device credential; challenge/HMAC authentication gates telemetry before the host adapter forwards it.
- An eight-entry NVS journal preserves boot, touch, online, and degraded events, replays them after host loss, and survives an app rewrite/reboot.
- Empty directory structure for future Rust crates, adapters, applications, schemas, examples, and tests.

## Not yet verified

- No allowlisted Windows service mutation has been selected or exercised.
- M5Tab5 Wi-Fi/ESP-Hosted connectivity, TLS, AP roaming/loss, and daemon-over-network restart behavior are unverified.
- The development unit stores its pairing key in unencrypted NVS; secure boot and flash encryption are disabled, so this is not production-protected identity storage.
- Sudden-power-loss journal behavior, storage exhaustion/wear, battery/power, thermal, and production recovery remain unverified.

## Next gate

Complete the remaining Phase 2 network and production-trust validation when credentials and an irreversible eFuse-hardening decision are available. Meanwhile, begin the narrow Phase 3 ESP32 firmware-test workflow without enabling unverified hazardous capabilities.
