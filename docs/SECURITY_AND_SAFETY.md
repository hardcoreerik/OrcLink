# Security and Safety

## Trust boundaries

`orclinkd` is the policy authority. UIs, AI planners, adapters, edge agents, discovered devices, and networks are untrusted until authenticated and authorized. Local network location is not identity. The UI is not a security boundary.

## Pairing and authentication

- New devices enter quarantine.
- Pairing requires operator-observed proof on both endpoints where hardware allows it.
- Credentials bind a stable node ID to a public key and explicit roles.
- Sessions use mutual authentication, freshness/nonces, anti-replay counters, and expiry.
- Rotation, revocation, loss, factory reset, and daemon recovery are documented and audited.
- M5Tab5 private keys use verified protected storage when available; lack of it is an explicit risk, not hidden by software claims.

## Authorization

Default deny. Grants are scoped by requester, node/component, capability namespace, argument constraints, operating mode, time, and connection state. Adapter capability declarations do not grant access. AI planners can propose actions but cannot approve their own requests or R3 actions.

## Secrets

Store secret material in OS-protected credential storage or verified device-protected storage. SQLite holds opaque references and nonsecret metadata. Never place secrets in manifests, command-line arguments, telemetry, URLs, routine logs, or exported audit bundles. Redaction occurs before persistence.

## Approvals

An approval binds requester, exact capability, target identity, normalized arguments or bounded ranges, risk, expiry, and one-time/reusable semantics. Material argument changes invalidate it. Approval UIs show intended effect, target, reversibility, verification, and stale/offline state.

M5Tab5 approval responses require a live authenticated path to the authoritative daemon in v0.1. Offline taps are recorded as operator intent only and cannot unlock centrally gated work.

## Risk classes

Use R0 through R3 as defined in `CAPABILITY_MODEL.md`. Policy may increase risk based on arguments or conditions. Unknown hardware function defaults to unavailable, not R0.

## Audit and recovery

Record identity, request, validation, policy decision, approval, dispatch, progress, cancellation, verification, result, and relevant configuration revisions. Audit records are append-only at the application layer and exported with integrity metadata. v0.1 does not claim tamper-proof storage.

After crashes or partitions, consequential commands with ambiguous outcomes become `Uncertain` and require reconciliation. Never replay a flash, motion, transmission, deletion, or similar action solely because acknowledgement was lost.

## Physical-device cautions

- OrcLink supplements, never replaces, physical interlocks, guards, emergency stops, thermal limits, fuses, current limits, watchdogs, and safe firmware defaults.
- Manual/emergency controls must be conspicuous, accessible, guarded against accidental activation, and tested on the actual device.
- On communication loss, each device follows its validated safe-state policy. “Fail off” is not universally safe for cooling, support, or emergency equipment.
- Calibration and hardware limits remain local and cannot be silently overridden by a remote policy update.

## Lasers and intense light

Laser output is out of v0.1. Future support requires appropriate physical interlocks, key control, emergency stop, fail-dark behavior where validated, output limits, scan-failure protection, controlled zones, and applicable regulatory review. An AI cannot enable output or bypass an interlock.

## Radio and SDR

Receiving and analysis may still expose sensitive data and local legal constraints. Transmission capabilities are consequential or safety/regulatory critical depending on power, band, location, and equipment. Require allowlisted profiles, verified hardware limits, operator authorization, and jurisdiction-specific review. Never allow generated arbitrary frequency/power settings.

## Printers, motion, and heat

Klipper/Moonraker integration begins read-only. Motion, heaters, job start/cancel, and emergency behavior require test hardware, physical observation, command-specific verification, and native firmware protections. OrcLink must not suppress native alarms.

## Minimum security gates before real control

- Threat model reviewed for local, remote, stolen-device, malicious-adapter, and replay cases.
- Pairing, revocation, expiry, and recovery exercised.
- Policy deny and approval binding tested.
- Logs inspected for secrets.
- Partition/restart leaves consequential actions uncertain rather than duplicated.
- Hardware-native safeguards verified on the exact equipment.
