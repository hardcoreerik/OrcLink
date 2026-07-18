# Project Status

**Status:** Documentation foundation and Phase 0A contract pack created; executable implementation has not begun.

## Created

- Initial architecture, capability, adapter, security, scope, roadmap, decision, and open-question documents.
- First-class M5Tab5 edge-node and operator-console design.
- Example M5Tab5 node and capability records.
- Minimum hardware-neutral node, event, capability, command, and adapter-envelope contracts for the simulator.
- Empty directory structure for future Rust crates, adapters, applications, schemas, examples, and tests.

## Not yet verified

- No Rust, Tauri, firmware, or UI code exists.
- No build or automated test has run.
- No M5Tab5 hardware/runtime characteristic has been confirmed.
- No adapter protocol has been exercised against a real process or device.

## Next gate

Review the Phase 0A contracts, then approve the smallest Rust simulator scaffold. M5Tab5 hardware validation remains the Phase 2 gate and does not block hardware-neutral Phase 0 work; all hardware-dependent capabilities stay disabled until verified.
