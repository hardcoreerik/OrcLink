# Project Status

**Status:** Phase 0 simulator core proves typed contract admission, command lifecycle, failure handling, restart reconciliation, and SQLite history.

## Created

- Initial architecture, capability, adapter, security, scope, roadmap, decision, and open-question documents.
- First-class M5Tab5 edge-node and operator-console design.
- Example M5Tab5 node and capability records.
- Minimum hardware-neutral node, event, capability, command, and adapter-envelope contracts for the simulator.
- Minimal Rust protocol crate that loads the simulator JSONL fixture and checks cross-record admission invariants.
- Deterministic online, offline, slow, and failing node simulator with policy denial, timeout, cancellation, verification, idempotency, and restart reconciliation checks.
- Empty directory structure for future Rust crates, adapters, applications, schemas, examples, and tests.

## Not yet verified

- No daemon, adapter process, Tauri, firmware, hardware, or UI code exists.
- No M5Tab5 hardware/runtime characteristic has been confirmed.
- No adapter protocol has been exercised against a real process or device.

## Next gate

Begin the narrow Windows host and Ollama read-only adapter proof. M5Tab5 hardware-dependent capabilities stay disabled until the Phase 2 validation report is complete.
