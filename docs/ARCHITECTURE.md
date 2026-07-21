# Architecture

## System boundary

`orclinkd` is the trusted control plane. UIs, AI planners, adapters, and edge agents are clients with explicit identities and permissions. A successful transport response is not proof of success; commands verify resulting state where the target permits it.

```text
Control Room ─┐
CLI ──────────┼── HTTP/WebSocket or local IPC ── orclinkd ── supervised adapters ── nodes
AI planner ───┤                                  │
M5Tab5 Console┘                                  ├── policy and approvals
                                                  ├── workflow executor
M5Tab5 agent ═ mutually authenticated session ═══├── registry and live state
                                                  └── SQLite event/audit store
```

## Daemon responsibilities

- Register stable node identities, endpoints, components, and capabilities.
- Ingest telemetry and events while retaining source and observation time.
- Validate, authorize, queue, execute, cancel, time out, and verify commands.
- Run deterministic workflows with explicit inputs, retries, cleanup, and approval gates.
- Persist configuration, last-known state, command history, approvals, and audit events.
- Supervise adapters and reject incompatible protocol versions.
- Serve snapshots over HTTP/local IPC and ordered updates over WebSocket.

The daemon survives UI closure. Restart recovery reconciles commands left in nonterminal states instead of assuming success or replaying them blindly.

## Adapters and process isolation

Adapters normally run out of process and communicate through the protocol in `ADAPTER_PROTOCOL.md`. Isolation limits crash impact, supports independent restarts, and allows future non-Rust integrations. v0.1 may use stdio JSON messages for local adapters because it is portable and inspectable; named pipes or local sockets are deferred until measured requirements demand them.

Each adapter owns transport-specific behavior. Core owns command identity, authorization, lifecycle, and audit. An adapter cannot grant itself permissions or silently add a capability.

The M5Tab5 agent is protocol-compatible in spirit but is a remote edge process, not a daemon-spawned sidecar. It uses a mutually authenticated network session and retains a restricted local queue.

## API and event flow

1. A node or adapter declares identity and capabilities.
2. The registry reconciles it with an approved record or quarantines it.
3. Telemetry updates the live projection and appends relevant events.
4. A client proposes a capability invocation or workflow.
5. Core validates schema, target ownership, current state, risk, authorization, and approvals.
6. The adapter executes with a command ID, deadline, and idempotency metadata.
7. Core records progress and independently verifies state when possible.
8. Clients receive ordered updates and may resnapshot after a gap.

The loopback HTTP API is versioned under `/api/v1`. It exposes daemon status,
latest nodes, an observed capability catalog with daemon-owned risk floors,
operations, bounded Chronicle queries, typed proposal intake, proposal receipts,
and an OpenAPI document. The `orclink-client` Rust crate and `orclinkctl` CLI use
this same surface. Direct adapter, serial, USB, and SQLite access remains outside
the application boundary.

Proposal admission is not dispatch. The v1 proposal route validates identity
syntax, target/capability presence, schema versions, risk floors, idempotency,
and secret-field rules before recording a lifecycle fact. R1-R3 proposals are
forced to `ApprovalRequired`; R0 proposals remain `Proposed` for policy
evaluation until grants and dispatch are implemented.

Events are immutable facts; current node state is a replaceable projection. Every event carries an event ID, source ID, source sequence when available, source time, daemon receipt time, kind, schema version, correlation ID, and payload.

## Workflows

Workflows are declared plans, not arbitrary scripts. Steps invoke known capabilities and can include conditions, deadlines, bounded retries, approval gates, and cleanup. v0.1 supports sequential workflows only; a graph scheduler, distributed transactions, and general rollback are deferred. Compensation is capability-specific and never described as guaranteed rollback.

M5Tab5 autonomous workflows are pre-authorized subsets installed by the daemon. They have fixed capability allowlists, parameters, expiry, resource bounds, and offline rules. The edge cannot synthesize new privileges while disconnected.

## Policy

Policy evaluates requester, target, capability, arguments, risk class, operating mode, connection state, and prior approval. Deny is the default for unknown identities, capabilities, schemas, or stale approvals. Safety-critical actions require a present human and device-native safeguards; policy cannot replace physical protection.

## Storage

**Decision:** start with one local SQLite database owned by `orclinkd`, using transactions and schema migrations. Store registry records, capability declarations, policies, workflow definitions/runs, commands, approvals, and audit events. Secrets are references to OS-protected storage, not plaintext database values. High-rate telemetry retention is bounded and may later move to a specialized store if measurements justify it.

## UI

Control Room is a Tauri client with topology, operations table, node inspector, command plan, approvals, alerts, and timeline. It is never the authorization boundary.

The M5Tab5 console uses the same API semantics with a smaller touch-first surface. Whether it is a native app, installed web application, or device-served web UI remains an **unknown** pending platform validation.

## Failure model

- Adapter crash: mark its nodes degraded, restart with backoff, reconcile before accepting commands.
- Daemon restart: recover durable state and mark uncertain commands for reconciliation.
- Client disconnect: running approved work continues unless policy says client presence is required.
- M5Tab5 disconnect: use last-known state in the central UI, reject stale approvals, execute only installed offline workflows, and sync with deduplication on reconnect.
- Clock drift: preserve source and receipt timestamps; ordering relies on per-source sequence numbers where available.
