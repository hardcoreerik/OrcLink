# Roadmap

Phases are evidence gates, not calendar promises. Do not begin hazardous integrations merely because an earlier UI looks complete.

## 0 — Contracts and simulator

Agree on minimal schemas and protocol semantics. Build simulated online, offline, slow, and failing nodes. Prove command lifecycle, policy denial, timeout, cancellation, verification, restart reconciliation, and SQLite timeline.

## 1 — Windows host and Ollama

Add supervised local adapters, read-focused telemetry, bounded operations, and the first Control Room views: operations table, node inspector, approvals, and timeline.

## 2 — M5Tab5 platform proof

Validate exact hardware and runtime facts. Prove identity storage, authenticated session, touch UI feasibility, one telemetry source, local journal durability, degraded display, and reconnect under network and daemon failure. Choose native or web UI only from evidence.

## 3 — ESP32 firmware-test workflow

Implement serial discovery, stable board identity, structured telemetry, approved firmware flashing, reboot/version verification, watchdog detection, and complete audit. Let M5Tab5 display/approve the run and optionally bridge one validated peripheral path.

**Measured proof (2026-07-18):** the first workflow is intentionally bound to the backed-up COM17 Tab5, one approved app offset, and one SHA-256 manifest. A wrong approval was denied before serial access. The real run verified USB identity, MAC binding, authenticated on-device update display/journal, esptool hash verification, rebooted node/version identity, watchdog-free logs, and a complete JSONL audit. A generic flasher and second peripheral bridge remain out of scope until another approved ESP target exists.

## 4 — M5Tab5 edge resilience

Add signed/bound offline workflow installation, bounded local execution, event deduplication, configuration revision reconciliation, credential rotation/revocation, storage pressure handling, and recovery testing.

**Measured proof (2026-07-18):** firmware 0.3.0 accepted a per-device-HMAC-signed `status_on_disconnect` configuration at monotonic revision 1 and rejected broader execution by enforcing a three-run NVS limit. Three real host-loss transitions journaled `workflow_run`, then stopped at the bound. Forced ring pressure reported nine dropped records; host acknowledgement cleared both pressure and pending records. Credential rotation persisted a new key, reauthenticated with it, journaled the change, and returned `PAIR_LOCKED` for the prior key. Daemon ingest deduplication is already covered by the SQLite API tests. Network partition behavior, protected device key storage, destructive power-loss, and wear testing remain explicit later hardening gates.

## 5 — Workflow engine and TheOrc integration

Add deterministic conditions, bounded retries, cleanup, and approval gates. Expose OrcLink capabilities to TheOrc as typed proposals. TheOrc remains a planner/client; it receives no arbitrary hardware or shell channel.

## 6 — Klipper through Moonraker

Begin with live status and alerts on test hardware. Add consequential commands individually after native safeguards, approvals, uncertain-result reconciliation, and command verification are proven.

## 7 — SDR and SatDump

Add device reservation, scheduled capture, artifact identity, storage checks, decoder invocation, and provenance. Signal-processing code remains outside core unless measurements justify otherwise.

## 8 — Meshtastic

Add paired node identity, packet/event ingestion, health, route visualization, privacy controls, and later tightly constrained transmit profiles subject to legal/safety review.

## 9 — DMX and lighting

Add scene/state control and validated timing outside the daemon's general workflow loop. M5Tab5 can provide portable manual controls with explicit connection and safety state.

## 10 — Laser control

Only after hardware interlocks, emergency stop, scan/output protections, regulatory review, and fail-safe behavior are independently proven. AI approval remains prohibited.

## 11 — BrightSign and broader integrations

Add content/status/deployment operations using the established adapter and audit model. Consider a third-party SDK or marketplace only after multiple external adapters expose a real need.
