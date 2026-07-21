# Security Policy

## Reporting a Vulnerability

If you believe you found a security vulnerability in OrcLink, do not open a public issue before a fix is ready. Send a confidential report to **hardcoreerik@gmail.com** with:

- a clear description of the issue and its impact;
- minimal reproduction steps or a proof of concept;
- the commit, release, firmware version, and hardware involved;
- relevant redacted logs or screenshots; and
- whether the issue requires local-network, USB, serial, or physical access.

You should receive an acknowledgment within 72 hours. Confirmed reports will be coordinated privately until a fix or mitigation is available. Reporter credit is welcome unless anonymity is requested. There is no bug bounty program at this time.

---

## Supported Versions

OrcLink is pre-release software. Security fixes apply to the latest commit on the default branch and, once releases begin, the latest published release. Older snapshots and firmware images are not maintained as separate security branches.

---

## In Scope

- bypassing requester, target, capability, risk-floor, approval, or idempotency checks;
- escaping a typed capability into arbitrary shell, USB, serial, radio, file, or network access;
- exposing Wi-Fi credentials, pairing secrets, HMAC keys, tokens, or protected configuration;
- forging adapter, ESP32-agent, proposal, approval, lifecycle, or Chronicle records;
- replay, downgrade, identity-confusion, origin-validation, or cross-node authorization flaws;
- unsafe retry or cancellation behavior that can repeat a consequential physical action;
- remote access beyond the documented loopback or authenticated private-network boundary; and
- firmware behavior that bypasses bounded offline authority or device-native safety requirements.

---

## Out of Scope

- vulnerabilities in third-party firmware, ESP-IDF, Ollama, Moonraker, Meshtastic, SatDump, or radio hardware unless OrcLink's integration makes the issue worse;
- claims that require unsupported hardware modifications or bypass documented physical safeguards;
- denial of service against a single local development instance without an authority or data boundary impact; and
- social engineering.

---

## Security Posture

OrcLink is an active hardware-research project and has not received a formal independent security or safety certification. Its intended posture is local-first, default-deny, typed, approval-aware, and auditable. Application policy supplements rather than replaces electrical protection, watchdogs, emergency stops, radio regulations, and device-native interlocks.

See [Security and Safety](docs/SECURITY_AND_SAFETY.md) for the system threat model and [Project Status](PROJECT_STATUS.md) for verified and unverified behavior.
