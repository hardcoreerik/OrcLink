# OrcLink

OrcLink is a Rust-based, local-first control plane for hardware, software services, AI runtimes, and technical workflows. It answers five operational questions: what is connected, what state is it in, what can it do, what is it doing, and what commands are safe to execute?

The daemon—not an AI model or UI—is the authority. Clients propose typed commands and workflows; `orclinkd` validates capabilities, policy, approvals, timeouts, and resulting state.

## Vision

Connect a workshop's machines, embedded devices, radios, AI runtimes, and developer tools through one inspectable and safe operational vocabulary. OrcLink remains useful without cloud access or AI planning.

M5Tab5 is a first-class platform in two roles:

- **Edge node/gateway:** sensors, local controls, telemetry, and Bluetooth/Wi-Fi/serial peripherals, with bounded offline workflows.
- **Portable operator console:** touch access to topology, status, approvals, alerts, and explicit emergency/manual controls.

Hardware-specific M5Tab5 details are not yet verified. The architecture does not assume a particular processor, OS, display, radio set, secure element, connector, or runtime until validated on target hardware.

## Goals

- Local-first operation with durable state and audit history.
- Stable node identity despite changing addresses or ports.
- Namespaced, schema-validated capabilities instead of arbitrary shell access.
- Explicit policy, approval, timeout, cancellation, and verification stages.
- Isolated, supervised adapters for Windows host, Ollama, ESP32, Klipper, and M5Tab5.
- Deterministic workflows that work without an AI planner.
- Useful degraded behavior when an M5Tab5 loses contact with `orclinkd`.
- Operator-visible provenance for every command and state change.

## Non-goals for v0.1

- A universal device ontology or public adapter marketplace.
- Cloud accounts, remote Internet exposure, or autonomous AI control.
- SDR sample processing, Meshtastic parsing, DMX timing, laser output, or BrightSign control.
- Replacing physical interlocks, emergency stops, or device-native safety systems.
- Committing to a native or web M5Tab5 UI before platform validation.

## Core components

- `orclinkd`: registry, command broker, workflows, policy, events, storage, and API.
- Control Room: Tauri desktop client for topology and operations.
- M5Tab5 agent: device identity, local I/O, telemetry, bounded execution, and synchronization.
- M5Tab5 Console: future native or web-based portable operator UI.
- Adapters: supervised integrations communicating over a versioned protocol.
- The Chronicle: durable event and audit timeline.

See [Architecture](docs/ARCHITECTURE.md), [M5Tab5 integration](docs/M5TAB5_INTEGRATION.md), and [v0.1 scope](docs/V0.1_SCOPE.md).

## First demonstration

“Prepare my ESP32 development environment for a firmware test.” OrcLink identifies the approved board, checks build output, requests approval before flashing, captures serial output, verifies reboot and firmware identity, detects watchdog resets, and records the complete timeline. An M5Tab5 displays status and approvals and may provide a paired serial/Wi-Fi bridge; loss of its upstream link must not turn an unapproved operation into an approved one.

## Design-truth labels

- **Decision:** accepted project direction unless superseded in `docs/DECISIONS.md`.
- **Assumption:** reasonable starting point requiring implementation evidence.
- **Unknown:** must be validated before it constrains design or code.

Implementation has not begun. See [PROJECT_STATUS.md](PROJECT_STATUS.md).
