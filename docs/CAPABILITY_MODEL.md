# Capability Model

## Core vocabulary

- **Node:** stable identity for an observable or controllable system.
- **Component:** addressable part owned by a node, such as an attached sensor.
- **Adapter/agent:** transport-specific process that observes or controls nodes.
- **Capability:** namespaced, versioned action with typed arguments/results and declared risk.
- **Telemetry:** current measurement with source, units, quality, and observation time.
- **Event:** immutable fact used for history, audit, and state projection.
- **Command:** one requested capability invocation against one target.
- **Workflow:** declared sequence of capability invocations and gates.
- **Policy:** rule deciding whether an invocation may proceed.

Use a small stable core and adapter-owned namespaces; do not build a universal class hierarchy.

## Capability declaration

A declaration includes name, schema version, argument/result schemas, a fixed R0-R3 risk floor, timeout bounds, concurrency rule, offline eligibility, idempotency behavior, cancellation support, and verification method. Capability names describe intent, for example `ollama.model.list`, `host.service.restart`, or `m5tab5.display.alert.show`. A capability without a fixed risk floor is not declared or invokable.

Declaring a capability does not authorize it. The daemon intersects declaration, requester grants, target policy, current mode, and approval.

## Telemetry

Telemetry includes node/component ID, metric name, typed value, unit when meaningful, source observation time, daemon receipt time, quality (`good`, `estimated`, `stale`, `invalid`), and optional source sequence. Adapter-specific extensions remain namespaced.

Last-known telemetry is never presented as live after its freshness limit. M5Tab5 cached values carry connection state and age.

## Command lifecycle

```text
Proposed → Validated → Authorized → Queued → Executing → Verifying
                                                    └→ Cancelling
Terminal: Succeeded | Failed | TimedOut | Cancelled | Rejected | Uncertain
```

- Validation checks schema, target ownership, declared capability, and deadline.
- Authorization checks identity, policy, risk, mode, and approval.
- Execution receives a unique command ID and idempotency key where supported.
- Verification observes intended state independently when possible.
- `Uncertain` is required when execution may have occurred but cannot safely be proven or replayed.

No transition skips audit recording. A command result distinguishes transport success from verified operational success.

## Risk levels

- **R0 Observation:** no state change; status, metrics, and logs.
- **R1 Reversible:** normally undoable, bounded state change; alerts, LEDs, model load, capture start.
- **R2 Consequential:** may interrupt work, consume substantial resources, transmit, delete, flash, or restart; explicit grants and usually approval.
- **R3 Safety-critical:** motion, heat, intense light, interlock changes, emergency functions, or regulated radio behavior; present-human gate plus device-native safeguards. AI cannot approve.

Risk is a floor set by the capability declaration. Policy may raise it based on arguments or context but cannot lower it below the declaration.

## Examples

| Capability | Risk | Notes |
|---|---:|---|
| `host.metrics.read` | R0 | Bounded metrics only |
| `ollama.model.list` | R0 | Read-only |
| `host.service.restart` | R2 | Allowlisted service and approval policy |
| `esp32.firmware.flash` | R2 | Approved artifact, identity check, verification |
| `klipper.temperature.read` | R0 | Read-only |
| `klipper.emergency_stop` | R3 | Must remain available, guarded against accidental use |
| `m5tab5.display.alert.show` | R1 | Bounded local display state |
| `m5tab5.telemetry.subscribe` | R0 | Rate and retention bounded |
| `m5tab5.workflow.install` | R2 | Signed definition, allowlist, expiry, resource bounds |
| `m5tab5.console.approval.respond` | R2 | Authenticated operator, online daemon, anti-replay; policy raises to the approved command's risk |
| `m5tab5.gpio.set` | — | Not declared until hardware/function validation establishes a fixed floor |

## M5Tab5 node modeling

One physical device has one stable node identity. Onboard functions are components. Attached peripherals are components only when their lifecycle and trust are inseparable from the gateway; otherwise they receive quarantined child-node identities. Changing Wi-Fi address or serial attachment does not change identity.

An M5Tab5 capability declares whether it is `online_only` or allowed under an installed `offline_policy`. An offline policy fixes capability names, arguments/ranges, expiry, invocation limits, and required local conditions. Approval responses are always online-only in v0.1.

The minimum machine-readable node, event, capability, command, and adapter-envelope contracts are in `schemas/protocol-v1.schema.json`. Hardware examples remain design examples until they conform to a validated target capability set.
