# Initial Architecture Decisions

These are project decisions, not verified implementation results. Supersede entries explicitly; do not silently rewrite history once implementation begins.

## D-001 — Rust daemon is the control authority

**Decision:** `orclinkd` owns registry, lifecycle, policy, workflows, persistence, and API. UIs and AI systems are clients.

**Rationale:** long-running concurrent I/O and strong typed boundaries are Rust-worthy work; separating authority prevents UI or planner shortcuts from bypassing policy.

## D-002 — Local-first and daemon/UI separation

**Decision:** core operation requires no cloud account, and closing a UI does not stop approved work or erase state.

**Rationale:** workshop and edge systems must remain operational during Internet or client loss.

## D-003 — Small core with namespaced capabilities

**Decision:** use stable node/command/event concepts plus adapter-owned capability namespaces and typed schemas.

**Rationale:** avoids a speculative universal device ontology while preserving validation and audit.

## D-004 — Out-of-process adapters by default

**Decision:** supervise adapters as sidecars; treat M5Tab5 as a remote agent with equivalent semantic boundaries.

**Rationale:** fault isolation, independent restart, and language flexibility without depending on a stable Rust plugin ABI.

## D-005 — SQLite first

**Decision:** one daemon-owned SQLite database for configuration, command state, workflows, and bounded event history.

**Rationale:** native transactions and simple deployment cover v0.1; specialized telemetry storage is premature.

## D-006 — Deterministic workflows before AI planning

**Decision:** workflows invoke declared capabilities; AI may propose plans but never executes arbitrary commands or self-approves.

**Rationale:** reproducibility, validation, least privilege, and useful operation without a model.

## D-007 — Verification and uncertainty are first-class

**Decision:** distinguish transport response, execution, and observed outcome; retain an `Uncertain` terminal state.

**Rationale:** retrying an ambiguous physical action can be worse than reporting uncertainty.

## D-008 — M5Tab5 has dual first-class roles

**Decision:** design it as both edge node/gateway and portable operator console from v0.1.

**Rationale:** the shared identity, offline journal, local controls, and touch surface should shape contracts early instead of being bolted on later.

## D-009 — M5Tab5 choices remain validation-driven

**Decision:** do not select native/web UI, transport, storage, secure-key mechanism, or peripheral surface until exact platform facts are measured.

**Rationale:** the supplied project context contains no verified hardware specification; guessing would hard-code risk.

## D-010 — Offline authority cannot expand

**Decision:** M5Tab5 may run only pre-installed bounded offline workflows; it cannot create approvals or new privileges while disconnected.

**Rationale:** partitions must not weaken policy.

## D-011 — Physical safety stays device-native

**Decision:** OrcLink policy supplements physical and firmware protections and cannot override required safeguards.

**Rationale:** application availability and network state are insufficient safety mechanisms.

## D-012 — Minimal v0.1 protocol transport

**Decision:** begin local sidecars with inspectable newline-delimited JSON over stdio; keep semantics transport-neutral for remote agents.

**Rationale:** simplest adequate implementation; move to pipes/sockets/binary framing only with evidence.

## D-013 — One JSON Schema contract pack for Phase 0

**Decision:** use JSON Schema Draft 2020-12 in `schemas/protocol-v1.schema.json`, with `$defs` for the minimum node, event, capability, command, and adapter-envelope contracts.

**Rationale:** one versioned file keeps the simulator boundary inspectable without introducing a schema build system or duplicated shared definitions. Schema validation does not define a canonical byte representation; approval signing remains blocked on a separate canonicalization decision.

## D-014 — Native M5Unified UI for the Tab5 proof

**Decision:** use the pinned pioarduino/M5Unified/M5GFX path for the Phase 2 native console while retaining the minimal ESP-IDF image as a low-level serial and recovery proof.

**Rationale:** the native path rendered the measured 1280x720 panel, mapped physical touch, used NVS, and survived reconnect testing. The current vendor ESP-IDF component graph did not compile because its codec and LVGL source APIs disagreed with its dependency versions.

## D-015 — Rust owns the public product surface

**Decision:** keep target firmware in the proven C++/C ESP32 environment while Rust owns `orclinkd`, the versioned local API, protocol types, reusable client SDK, operator CLI, policy admission, lifecycle, persistence, and adapter supervision.

**Rationale:** applications should integrate once with an authoritative typed API instead of taking direct USB, serial, or adapter ownership. This increases reusable Rust surface area without rewriting measured firmware solely to change repository language statistics.

## D-016 — Proposal intake precedes dispatch

**Decision:** the first mutation API persists typed proposals and returns `Proposed`, `ApprovalRequired`, or `Rejected`; it does not treat admission as authorization or execution.

**Rationale:** target/capability validation, fixed risk floors, idempotency, secret rejection, and audit must exist before adapters receive commands. Dispatch remains a later gate with explicit grants, approvals, deadlines, and verification.
