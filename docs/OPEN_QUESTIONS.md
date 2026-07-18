# Open Questions

Resolve questions with links to vendor documentation, measured results, or recorded design tests. Do not convert assumptions into facts by repetition.

## M5Tab5 hardware and platform

- What exact product/board revision is the target, and what authoritative documentation applies?
- What processor architecture, RAM, persistent storage, and usable resource budget exist?
- What OS/RTOS, SDK, bootloader, compiler, package format, and licenses apply?
- Which Wi-Fi, Bluetooth, USB, UART, GPIO, and expansion modes are physically present and exposed by the SDK?
- What display/touch capabilities, orientation, graphics APIs, fonts, accessibility hooks, and kiosk modes exist?
- Is there a browser/webview/PWA runtime suitable for a durable offline console?
- Can the platform run a Rust agent directly, call Rust through FFI, or should the agent use the vendor-native language?
- Is secure boot available? Are updates signed, atomic, recoverable, and rollback-protected?
- Is protected private-key storage available? What physical extraction resistance can honestly be claimed?
- What unique identifiers are stable, nonspoofable enough for discovery, and safe to expose?
- What battery, charging, sleep/wake, thermal, watchdog, and power-loss behavior must the agent handle?
- What storage endurance and filesystem guarantees constrain the offline journal?

## M5Tab5 application architecture

- One package or separately supervised edge agent and UI?
- Native touch UI, installed web UI, or hybrid after the platform proof?
- Direct console-to-daemon connection, edge-agent proxy, or both?
- Which functions remain available while locked, offline, low-power, or storage-constrained?
- What local gesture/confirmation is required for approvals and emergency/manual controls?
- How should factory reset revoke credentials when the daemon is unreachable?
- What is the smallest safe set of offline workflows, and how are expiry and invocation counts enforced against clock rollback?
- Which attached peripherals become components versus independent child nodes?

## Protocol and core

- JSON Schema dialect and canonicalization rules for approval argument binding?
- Exact event retention limits and telemetry coalescing policy?
- HTTP/WebSocket framework and local IPC choice after simulator measurements?
- How are daemon trust roots created, backed up, and recovered without weakening device identity?
- Which command classes are safely idempotent, and what adapter evidence proves that?
- How are configuration conflicts represented when device-local safety/calibration values differ?
- Is a single-user OS identity sufficient for v0.1, or are multiple operator roles required immediately?

## Integrations

- Which Windows metrics/service APIs work without elevated privileges, and what elevation boundary is acceptable?
- Which Ollama endpoints/versions form the supported minimum?
- What generic ESP32 wire schema serves the first real device without becoming a universal protocol?
- Which Moonraker read-only events are necessary for the Klipper milestone?
- Which exact ESP32 and Klipper test hardware may be controlled without risking production equipment?

## Product and operations

- Supported Windows/Linux versions for `orclinkd` and Control Room?
- Installer/service lifecycle, database backup, and log/export locations?
- Threat model and local-network exposure defaults?
- Naming and public licensing decisions?
- What measured need would justify a public adapter SDK/marketplace?
