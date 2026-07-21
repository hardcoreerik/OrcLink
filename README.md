<div align="center">

<img src="apps/control-room/ui/assets/theorc/icon.png" alt="TheOrc emblem" width="180" />

# OrcLink

[![Rust](https://img.shields.io/badge/core-Rust-76B900?style=for-the-badge&logo=rust&logoColor=white)](#quick-start)
[![ESP32-P4](https://img.shields.io/badge/edge-ESP32--P4-9DFF00?style=for-the-badge&logo=espressif&logoColor=111111)](#measured-hardware-proofs)
[![Local first](https://img.shields.io/badge/network-local--first-161616?style=for-the-badge)](#authority-before-automation)
[![Hardware proof](https://img.shields.io/badge/status-live_hardware_proofs-569CD6?style=for-the-badge)](#what-works-today)

**The edge-control plane for [TheOrc](https://github.com/hardcoreerik/TheOrc).**

TheOrc can plan. OrcLink gives it a typed, policy-gated, auditable path to hardware—without handing an AI arbitrary USB, shell, or device access.

[Explore OrcLink](https://hardcoreerik.github.io/OrcLink/) · [Architecture](docs/ARCHITECTURE.md) · [Project status](PROJECT_STATUS.md) · [Safety model](docs/SECURITY_AND_SAFETY.md)

</div>

---

## What is this thing?

OrcLink is a Rust-based, local-first control plane for hardware, services, AI runtimes, radios, and technical workflows. It answers the questions that matter when software reaches into the physical world:

- What is connected?
- What state is it actually in?
- What has it declared it can do?
- What is running right now?
- What evidence proves the outcome?

The daemon—not the UI and not an AI model—is the authority. Clients propose typed capability invocations; `orclinkd` validates identity, schema, policy, risk, approvals, timeouts, and observed results. Closing the dashboard does not erase state or quietly stop approved work.

## Authority before automation

```text
TheOrc / UI / CLI
      │ proposes typed intent
      ▼
┌─────────────────────────────────────────────────────────────┐
│ orclinkd                                                    │
│ identity → validation → policy → approval → lifecycle       │
│                              └──────────────→ Chronicle      │
└─────────────────────────────────────────────────────────────┘
      │ dispatches only an authorized capability
      ▼
isolated adapter / authenticated ESP32 agent
      │
      ▼
hardware, service, radio, or local runtime
```

There is no generic “AI shell” or universal raw-device escape hatch. Adapters own transport details, but only the daemon owns permission and lifecycle. Ambiguous consequential outcomes become `Uncertain` instead of being blindly replayed.

## What works today

| Surface | Current evidence |
|---|---|
| `orclinkd` | Ingests adapter records, owns SQLite state, and serves loopback snapshots, operations, and timeline APIs. |
| Control Room | A Tauri desktop console reads daemon-owned state and clearly separates proposals from authority. |
| Windows + Ollama | Local sidecars report bounded host and installed-model state without requiring a cloud account. |
| M5Tab5 | 1280×720 touch UI, stable identity, authenticated telemetry, bounded offline workflows, NVS journal, Wi-Fi inventory/provisioning, and firmware-test proof. |
| Waveshare ESP32-P4 | 2.4-inch ILI9341 display, XPT2046 touch, live Ethernet/DHCP telemetry, and ESP32-C6 Wi-Fi inventory on the measured dev kit. |
| RTL-SDR.COM V4 | Direct high-speed USB enumeration is proven on M5Tab5; the clean-room native transfer path is in progress. |
| Moonraker | Read-only server, printer, object-list, and selected object-status queries. No motion or heater control. |
| Meshtastic | Receive-only, identity-bound sidecar with privacy-safe topology. No transmit capability. |

These are deliberately narrow claims. A successful compile is not a hardware proof, and enumeration is not IQ reception. The current evidence and remaining gaps live in [PROJECT_STATUS.md](PROJECT_STATUS.md).

## Measured hardware proofs

### Waveshare ESP32-P4-Module-DEV-KIT

The measured revision 1.3 board now runs a touch-driven, three-page OrcLink dashboard:

- Ethernet page: DHCP address, link speed, and duplex state
- Wi-Fi page: ESP-Hosted readiness, scan count, provisioning state, and station address
- Device page: uptime, free heap, display state, and touch state

The attached 320×240 Raspberry Pi-style display is identified as ILI9341 + XPT2046. The test unit acquired Ethernet DHCP at 100 Mbps full duplex and completed a passive Wi-Fi scan through its ESP32-C6 sidecar. Credentials are accepted only through the authenticated `SET_WIFI` path; they are not compiled into firmware or exposed in telemetry.

See [the board bring-up and pin map](firmware/waveshare-p4/README.md).

### M5Tab5 + RTL-SDR.COM V4

M5Tab5 is both an edge agent and portable operator console. The live unit has proven direct RTL-SDR.COM V4 high-speed USB enumeration without a PC owning the radio. The next hardware gate is a bounded native IQ capture; tuner initialization, calibrated reception, and a valid over-the-air signal remain unverified.

See the [ESP32 universal driver plan](docs/ESP32_RTL_SDR_DRIVER_PLAN.md) and [clean-room transfer specification](docs/RTL_SDR_V4_CLEAN_ROOM_SPEC.md).

## Why OrcLink exists

TheOrc is the warboss: planning, coordination, and the HIVE. OrcLink is the field authority: stable device identity, adapters, policy gates, offline limits, and the Chronicle of what actually happened.

That split makes one driver useful to many applications. Apps call declared OrcLink capabilities instead of competing for USB ownership. A future RTL-SDR service can expose bounded tune, gain, capture, and stream operations while one authenticated component owns the physical device.

## Quick start

### Requirements

- Rust stable with a native C/C++ toolchain (MSVC Build Tools on Windows)
- Node.js and npm for Control Room development
- PlatformIO only when building ESP32 firmware

### Build and test the workspace

```powershell
git clone https://github.com/hardcoreerik/OrcLink.git
cd OrcLink
cargo fmt --all -- --check
cargo check --workspace
cargo test --workspace
```

### Start the local daemon

Build the workspace once so the host and Ollama adapters sit beside `orclinkd`, then start the loopback API:

```powershell
cargo build --workspace
cargo run -p orclink-daemon
```

The daemon listens on `http://127.0.0.1:4017` by default.

### Launch Control Room

```powershell
cd apps/control-room
npm install
npm run tauri -- dev
```

The Control Room is a client. Browser or UI state never grants authority.

## Repository map

| Path | Purpose |
|---|---|
| `crates/orclink-daemon` | Authoritative daemon, SQLite projection, and loopback API |
| `crates/orclink-protocol` | Protocol-v1 types, invariants, and TheOrc proposal boundary |
| `crates/orclink-core` | Deterministic simulator and sequential workflow executor |
| `crates/orclink-firmware-test` | Identity-bound, approval-gated ESP32 flashing workflow |
| `adapters/` | Isolated Windows, Ollama, M5Tab5, Moonraker, RTL-SDR, and Meshtastic integrations |
| `apps/control-room` | Tauri desktop operations console |
| `firmware/m5tab5` | M5Tab5 edge agent, console, Wi-Fi, journal, and RTL-SDR work |
| `firmware/waveshare-p4` | Waveshare P4 display, touch, Ethernet, and Wi-Fi proof |
| `schemas/` | Versioned JSON Schema contract pack and examples |
| `docs/` | Architecture, safety, integration records, decisions, and measured status |
| `site/` | GitHub Pages landing page |

## Safety is part of the protocol

- Unknown identities, schemas, capabilities, and targets are denied by default.
- Capability risk is fixed at an R0–R3 floor; policy can raise it, never lower it.
- AI can propose work but cannot approve its own consequential actions.
- Offline ESP32 authority cannot expand beyond signed, bounded workflows.
- OrcLink supplements physical interlocks, watchdogs, emergency stops, and device-native alarms; it never replaces them.
- Radio transmission, motion, heat, intense light, and similar surfaces stay unavailable until their exact hardware and safety gates are proven.

Read [Security and Safety](docs/SECURITY_AND_SAFETY.md) and the [Capability Model](docs/CAPABILITY_MODEL.md) before adding a control surface.

## Project posture

Phases 0–8 have narrow implementation proofs. The active hardware gate is bounded native RTL-SDR.COM V4 IQ capture on M5Tab5. DMX and laser phases are explicitly deferred.

This is an active hardware-research project, not a claim of production safety certification. Every live-hardware milestone records what was measured, what merely compiled, and what remains unknown.

## The warband

- [TheOrc](https://github.com/hardcoreerik/TheOrc) — local AI orchestrator and HIVE boss
- [TheOrc Companion](https://github.com/hardcoreerik/TheOrcCompanion) — Android field node, scout, and relay
- **OrcLink** — edge-control authority connecting plans to real systems

Created by [Erik / hardcoreerik](https://github.com/hardcoreerik), with AI-assisted development. The architecture, hardware choices, validation direction, and final decisions are human-led.
