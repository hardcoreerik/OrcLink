# Adapter Protocol

## Scope

The protocol connects `orclinkd` to supervised local adapters and informs the compatible remote M5Tab5 agent protocol. v0.1 favors newline-delimited JSON over stdio for local sidecars: easy to inspect, implement, and supervise. Remote agents use framed messages over an authenticated session; payload semantics remain shared.

## Process model

The daemon starts configured local adapters with a minimal environment and no secrets on command lines. Each adapter has one stdin/stdout protocol channel; diagnostics go to stderr as structured logs. The daemon applies startup deadlines, heartbeat limits, restart backoff, and a crash-loop cutoff. Adapter processes do not write the core database.

## Manifest

At startup the adapter sends:

```json
{
  "type": "hello",
  "message_id": "message_hello_01",
  "protocol_version": { "major": 1, "minor": 0 },
  "payload": {
    "adapter": { "id": "ollama.local", "version": "0.1.0" },
    "features": ["discovery", "telemetry", "commands", "cancellation"],
    "capability_namespaces": ["ollama"],
    "config_schema_version": 1
  }
}
```

The daemon responds with accepted protocol/features, adapter instance ID, limits, heartbeat interval, and configuration revision. A major mismatch is fatal; unknown optional minor features are ignored only after negotiation.

## Envelope

Every message has `type`, `message_id`, `protocol_version`, and `payload`. Replies include `in_reply_to`. Operational messages also include relevant `adapter_instance_id`, node ID, correlation/command ID, source sequence, and timestamps. Message size and rate are bounded.

The authoritative Phase 0 contract is `schemas/protocol-v1.schema.json`. It uses one envelope for startup and operational messages; message-specific payload schemas can be added only when the simulator needs them.

## Discovery and registration

Adapters report candidates with identity evidence, endpoints, and observed metadata. Core decides whether a candidate matches an approved node, needs operator confirmation, or remains quarantined. Adapters never choose permanent node IDs solely from IP, MAC, hostname, or port.

After approval, an adapter declares capabilities with schema versions and execution metadata. Removing a capability is explicit and causes new invocations to fail closed.

## Telemetry and events

- Snapshots establish current state after startup/reconnect.
- Updates carry monotonic per-source sequence numbers where possible.
- The daemon acknowledges durable checkpoints, not every metric.
- Gaps trigger snapshot/resync rather than guessed state.
- Backpressure permits dropping/coalescing declared ephemeral telemetry, never command/audit events.

## Command invocation

The daemon sends command ID, target, capability, validated arguments, deadline, idempotency key, and requested verification data. The adapter replies with accepted/rejected, progress, result, and cancellation outcome. Results include whether the action was executed, observed, or uncertain.

Adapters reject unknown capabilities, schema versions, targets, or expired deadlines. They do not reinterpret invalid arguments. Consequential commands are not automatically retried unless the capability explicitly defines safe idempotency.

Cancellation is best effort and has its own acknowledgement. A cancelled request may still require reconciliation if the physical action already began.

## Errors

Errors have stable code, human-safe message, retryability, optional field path, and structured details with secrets removed. Initial codes:

- `INVALID_MESSAGE`, `UNSUPPORTED_VERSION`, `SCHEMA_MISMATCH`
- `UNKNOWN_NODE`, `UNKNOWN_CAPABILITY`, `TARGET_UNAVAILABLE`
- `UNAUTHORIZED`, `APPROVAL_REQUIRED`, `DEADLINE_EXCEEDED`
- `CONFLICT`, `BUSY`, `TRANSPORT_FAILURE`, `DEVICE_FAILURE`
- `CANCEL_UNSUPPORTED`, `RESULT_UNCERTAIN`, `INTERNAL`

## Heartbeats and health

Heartbeat messages report adapter instance, monotonic uptime, queue depth, and degraded reasons. Missing heartbeats marks nodes degraded before restart. A heartbeat proves process liveness, not target health; target observations remain separate.

Remote M5Tab5 heartbeats additionally carry boot ID, agent instance ID, last durable local sequence, configuration revision, and journal pressure.

## Security

Local adapter executable/config paths are allowlisted and file permissions verified where practical. Remote agents use mutual authentication, nonce/session binding, anti-replay sequencing, and credential revocation. Secrets are referenced by opaque handles; protocol logs redact credentials and sensitive payload fields.

## Versioning

- Protocol major: incompatible envelope/semantic change.
- Protocol minor: backward-compatible optional feature.
- Capability schema version: independent per capability.
- Adapter version: implementation release, not compatibility proof.

Persist negotiated versions with command records. Do not silently transform a consequential command between incompatible schemas.
