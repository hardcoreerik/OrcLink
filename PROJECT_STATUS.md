# Project Status

**Status:** Phases 0 through 8 have narrow implementation proofs. Phases 9 and 10 are explicitly deferred. M5Tab5, Waveshare ESP32-P4, firmware-test, RTL-SDR, and Meshtastic paths have live hardware evidence; Moonraker lacks an identified live target.

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
- `orclinkd` owns adapter collection, persists records in SQLite, and serves a versioned loopback API for status, latest nodes, capability/risk catalog, operations, bounded Chronicle queries, proposal intake/receipts, and OpenAPI discovery. Proposal admission atomically records the receipt and Chronicle fact while enforcing current target/capability presence, fixed risk floors, idempotency, higher-risk approval, direct-secret rejection, and foreign-browser-origin denial without claiming dispatch.
- A reusable `orclink-client` Rust SDK and `orclinkctl` operator CLI expose status, doctor, nodes, capabilities, operations, timeline/watch, proposal, and OpenAPI workflows without direct adapter or hardware access.
- Minimal Tauri Control Room reads daemon-owned node snapshots, operations, and timeline with visible online/offline state.
- M5Tab5 firmware boots on the measured unit, renders a 1280x720 touch UI, reports structured telemetry, and preserves its stable node identity.
- Physical-USB pairing provisions one device credential; challenge/HMAC authentication gates telemetry before the host adapter forwards it.
- An eight-entry NVS journal preserves boot, touch, online, and degraded events, replays them after host loss, and survives an app rewrite/reboot.
- The first firmware-test workflow binds COM17 VID/PID, target MAC, flash offset, version, and SHA-256; rejects a bad approval before device access; displays the approved run on M5Tab5; verifies the app-only flash, reboot identity/version, watchdog-free boot, and JSONL audit.
- M5Tab5 firmware 0.7.2 proves signed bounded offline workflow installation, a three-run execution limit, journal-pressure accounting, host acknowledgement, persistent credential rotation, nonblocking read-only ESP-Hosted Wi-Fi inventory, authenticated file-based Wi-Fi provisioning, honest read-only battery telemetry, and direct High-Speed enumeration of the attached RTL-SDR.COM V4 on the real unit.
- A measured Waveshare ESP32-P4-Module-DEV-KIT revision 1.3 drives the attached 320x240 ILI9341 display and XPT2046 touch controller, exposes a three-page local dashboard, acquires Ethernet DHCP at 100 Mbps full duplex, and initializes its ESP32-C6 Wi-Fi sidecar for passive station scanning. Its serial agent reuses the M5Tab5 pairing, HMAC authentication, heartbeat, and authenticated Wi-Fi provisioning boundary.
- The deterministic workflow executor proves bounded retries, cleanup, approval blocking, and a strict typed TheOrc proposal boundary with no arbitrary shell channel.
- The Moonraker adapter exposes only read-only server, printer, object-list, and selected object-status queries; its default local endpoint refused connection.
- The RTL-SDR adapter reserves device 0, checks storage, supports scheduled bounded CU8 capture, hashes exact artifacts, verifies provenance before decode, and invokes an explicit SatDump pipeline under a timeout. A real V4 capture and signal-unverified decoder smoke completed.
- The receive-only Meshtastic adapter binds COM11 by USB serial and node ID, emits health and privacy-safe hop topology, strips sensitive packet content, and exposes no transmit operation. A live Heltec V4 snapshot reported 53 known nodes.
- Empty directory structure for future Rust crates, adapters, applications, schemas, examples, and tests.

## Not yet verified

- No allowlisted Windows service mutation has been selected or exercised.
- Proposal dispatch, requester grants, signed approvals, adapter command sessions, cancellation, and independent outcome verification are not yet wired into the local API; valid intake remains `Proposed` or `ApprovalRequired` rather than `Authorized`.
- M5Tab5 ESP-Hosted initialization and passive station scanning are verified; Wi-Fi association, TLS, AP roaming/loss, and daemon-over-network restart behavior are unverified.
- Waveshare P4 Ethernet/DHCP and passive Wi-Fi scanning are verified; Wi-Fi association, TLS, and an authenticated OrcLink socket transport over either interface are unverified.
- The development unit stores its pairing key in unencrypted NVS; secure boot and flash encryption are disabled, so this is not production-protected identity storage.
- Sudden-power-loss journal behavior, storage exhaustion/wear, battery/power, thermal, and production recovery remain unverified.
- The Tab5 enumerates the V4 natively, but R828D initialization, bounded IQ transfer, a valid over-the-air signal, and calibrated antenna/PPM/gain profile are unverified.
- No live Meshtastic packet arrived during the bounded listener, so actual packet-event ingestion remains unobserved.
- No DMX interface or lighting fixture has been identified for Phase 9.
- Phase 9 DMX and Phase 10 laser work are deferred by operator direction, not complete.

## Next gate

The active hardware gate is a bounded native IQ capture from the RTL-SDR.COM
V4 on the M5Tab5. DMX and laser work remain deferred by operator direction.
