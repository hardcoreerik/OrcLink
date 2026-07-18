# Project Status

**Status:** Phase 0 simulator evidence is complete; Phase 1 Windows host and Ollama adapter proof is in progress.

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
- Empty directory structure for future Rust crates, adapters, applications, schemas, examples, and tests.

## Not yet verified

- No daemon, adapter process, Tauri, firmware, hardware, or UI code exists.
- No M5Tab5 hardware/runtime characteristic has been confirmed.
- No allowlisted Windows service mutation has been selected or exercised.

## Next gate

Begin the narrow Windows host and Ollama read-only adapter proof. M5Tab5 hardware-dependent capabilities stay disabled until the Phase 2 validation report is complete.
