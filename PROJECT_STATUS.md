# Project Status

**Status:** Phase 0 simulator evidence is complete; Phase 1 read-only Windows host and Ollama adapter proof is in progress.

## Created

- Initial architecture, capability, adapter, security, scope, roadmap, decision, and open-question documents.
- First-class M5Tab5 edge-node and operator-console design.
- Example M5Tab5 node and capability records.
- Minimum hardware-neutral node, event, capability, command, and adapter-envelope contracts for the simulator.
- Minimal Rust protocol crate that loads the simulator JSONL fixture and checks cross-record admission invariants.
- Deterministic online, offline, slow, and failing node simulator with policy denial, timeout, cancellation, verification, idempotency, and restart reconciliation checks.
- Real local Windows host and Ollama sidecars emit protocol-v1 hello and node snapshot records without cloud access.
- Empty directory structure for future Rust crates, adapters, applications, schemas, examples, and tests.

## Not yet verified

- No daemon, adapter process, Tauri, firmware, hardware, or UI code exists.
- No M5Tab5 hardware/runtime characteristic has been confirmed.
- No bounded adapter command has been exercised against a real target.

## Next gate

Begin the narrow Windows host and Ollama read-only adapter proof. M5Tab5 hardware-dependent capabilities stay disabled until the Phase 2 validation report is complete.
