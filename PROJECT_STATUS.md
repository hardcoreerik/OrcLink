# Project Status

**Status:** Phase 0B proves the Phase 0A contracts can drive typed Rust ingestion and simulated-node admission.

## Created

- Initial architecture, capability, adapter, security, scope, roadmap, decision, and open-question documents.
- First-class M5Tab5 edge-node and operator-console design.
- Example M5Tab5 node and capability records.
- Minimum hardware-neutral node, event, capability, command, and adapter-envelope contracts for the simulator.
- Minimal Rust protocol crate that loads the simulator JSONL fixture and checks cross-record admission invariants.
- Empty directory structure for future Rust crates, adapters, applications, schemas, examples, and tests.

## Not yet verified

- No daemon, adapter process, Tauri, firmware, hardware, or UI code exists.
- No M5Tab5 hardware/runtime characteristic has been confirmed.
- No adapter protocol has been exercised against a real process or device.

## Next gate

Extend the simulator only when the next Phase 0 contract behavior is approved. M5Tab5 hardware validation remains the Phase 2 gate and does not block hardware-neutral Phase 0 work; all hardware-dependent capabilities stay disabled until verified.
