# OrcLink Project Memory

## Purpose and authority

- OrcLink is a Rust-based, local-first control plane for hardware, services, AI runtimes, and technical workflows.
- `orclinkd` is the authority for identity, policy, approvals, command lifecycle, workflows, persistence, and audit. UIs, AI planners, adapters, and edge agents are clients and must not bypass it.
- AI may propose typed capability invocations; it must never gain an arbitrary shell/hardware channel, approve its own consequential work, or expand authority while offline.
- Prefer deterministic, declared workflows and namespaced, schema-validated capabilities over generic scripting or a universal device ontology.

## Sources of truth

Read these before making architectural or scope changes:

1. `PROJECT_STATUS.md` for current implementation evidence, gaps, and the next gate.
2. `docs/DECISIONS.md` for accepted architectural decisions. Add an explicit superseding decision; do not silently rewrite history.
3. `docs/SECURITY_AND_SAFETY.md` and `docs/CAPABILITY_MODEL.md` for trust, approval, risk, and lifecycle invariants.
4. `docs/ARCHITECTURE.md` and `docs/ADAPTER_PROTOCOL.md` for component ownership and protocol semantics.
5. The relevant integration document under `docs/` plus nearby tests for device-specific work.

The final sentence of `README.md` saying implementation has not begun is stale. The code, tests, `PROJECT_STATUS.md`, and measured entries in `docs/ROADMAP.md` show narrow proofs through Phase 8. Preserve the documentation truth labels: **Decision**, **Assumption**, **Unknown**, and clearly identified measured/repo proof.

## Repository map

- `crates/orclink-protocol`: protocol-v1 types, validation boundary, and TheOrc proposal types.
- `crates/orclink-core`: simulator and deterministic sequential workflow executor.
- `crates/orclink-daemon`: daemon library/binary, SQLite-owned state, and loopback read API.
- `crates/orclink-firmware-test`: tightly bound, audited firmware-test workflow.
- `adapters/`: out-of-process integrations for Windows host, Ollama, M5Tab5, Moonraker, and RTL-SDR. Meshtastic is currently a receive-only Python sidecar.
- `apps/control-room`: Tauri desktop client; it displays daemon-owned state and is never an authorization boundary.
- `firmware/m5tab5`: ESP-IDF serial/recovery proof and the pinned PlatformIO/M5Unified touch UI proof.
- `schemas/protocol-v1.schema.json`: authoritative Phase 0 JSON Schema contract pack.
- `.orclink/`: local captures, credentials, audit runs, and hardware artifacts. Treat as generated/sensitive local state, not source.

## Non-negotiable invariants

- Default-deny unknown identities, schemas, capabilities, targets, and stale approvals.
- A capability declaration has a fixed R0-R3 risk floor; policy may raise but never lower it. Declaring a capability does not authorize it.
- Bind approvals to requester, exact target/capability, normalized arguments or bounded ranges, risk, expiry, and reuse semantics. Material argument changes invalidate approval.
- Distinguish transport success, execution, and independently observed outcome. Ambiguous consequential results become `Uncertain`; never blindly replay flash, motion, transmission, deletion, heat, or similar actions after lost acknowledgement.
- Events are immutable facts; current state is a replaceable projection. Preserve provenance, correlation IDs, source/receipt times, and source sequence where available.
- Adapters own transport details but cannot grant permissions, invent capabilities, or write the core database.
- Store secrets in protected credential storage when available. Never put secrets in manifests, CLI arguments, telemetry, URLs, routine logs, fixtures, or committed files. Redact before persistence.
- Physical/firmware safeguards, interlocks, emergency stops, watchdogs, and device-native alarms remain authoritative. Application policy does not replace them.
- M5Tab5 offline execution is limited to pre-installed, signed, bounded workflows. Offline UI input cannot create central approval or new privilege.
- Moonraker remains read-only until a real test target and command-specific safety gates exist. Meshtastic remains receive-only. DMX and laser phases are deferred and must not be implemented without an explicit operator decision and required hardware/safety evidence.

## Development and validation

- Workspace-wide baseline: `cargo fmt --all -- --check`, `cargo check --workspace`, and `cargo test --workspace`.
- For Rust changes, run focused package tests first, then the workspace baseline when practical. Run `cargo clippy --workspace --all-targets -- -D warnings` for release-quality or adapter-boundary changes.
- Control Room JavaScript syntax check: from `apps/control-room`, run `npm run check`. Tauri commands use the pinned local CLI via `npm run tauri -- ...`.
- Protocol changes must keep Rust types, `schemas/protocol-v1.schema.json`, fixtures/examples, and cross-record invariant tests synchronized. Treat protocol-major, protocol-minor, capability-schema, and adapter versions as distinct concepts.
- Add regression tests for lifecycle, policy denial, approval binding, idempotency, timeouts/cancellation, restart reconciliation, deduplication, redaction, and uncertain outcomes as relevant.
- Do not claim live hardware success from a compile, mock, transport response, or decoder smoke. Record exact device identity, bounded procedure, artifact hashes where applicable, observed result, and remaining unknowns.

## M5Tab5 hardware rules

- Follow `firmware/m5tab5/README.md` and `docs/M5TAB5_VALIDATION_REPORT.md`; this code targets the measured Tab5 v1.3/pre-v3 ESP32-P4 unit.
- The UI proof pins pioarduino 55.03.38-1, Arduino 3.3.8, M5Unified 0.2.15, and M5GFX 0.2.21. PlatformIO must use Python 3.10-3.13 and its matching ESP-P4 toolchain.
- Leave display/touch autodetection to `M5.begin()`, call `M5.update()` once per loop, set display rotation once, and do not add manual GT911 or PI4IOE reset sequences.
- Do not use esptool `--force`. Before writing hardware, preserve and hash a complete flash backup. Edit `sdkconfig.defaults`, not generated `sdkconfig` or `build/` output.
- COM numbers are observations, not stable identity. Bind devices using the documented USB identity, MAC/node identity, manifest, version, offset, and artifact digest as applicable.
- The development unit uses unencrypted NVS for credentials; never describe it as production-protected storage.

## Current direction

- Phases 0-8 have narrow proofs; Phases 9-10 are deferred.
- The active hardware gate is a bounded native IQ capture from the RTL-SDR.COM V4 attached directly to M5Tab5.
- Keep claims narrow: native V4 USB enumeration is proven, but R828D initialization, bounded IQ transfer, calibrated reception, valid over-the-air signal, and antenna/PPM/gain profiles remain unverified unless newer measured evidence explicitly supersedes this memory.
- When code or hardware evidence changes status, update `PROJECT_STATUS.md` and the relevant integration/roadmap documentation in the same change so claims remain synchronized.
