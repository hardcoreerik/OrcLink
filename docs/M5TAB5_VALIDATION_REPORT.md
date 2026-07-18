# M5Tab5 validation report

Date: 2026-07-18

Target: M5Stack Tab5 on COM17

Status: native display/touch and authenticated direct-USB resilience proof verified; network and production hardening remain open

## Repo-verified

- The firmware builds with ESP-IDF 5.5.3 for `esp32p4` and pre-v3 silicon,
  minimum revision v0.1. That silicon family selection matches the official
  M5Stack Tab5 demo configuration.
- The original ESP-IDF serial image and the native pioarduino/M5Unified UI both
  build for the measured pre-v3 ESP32-P4 silicon. The authenticated UI build
  uses 545,840 bytes of its 6.25 MiB app partition and 29,752 bytes of internal
  RAM; its flashed `firmware.bin` SHA-256 is
  `6FFE0B137A1D84FF296DA332D9C5810AC427C85AA80B17E4448C016EADDC41C7`.
- The firmware installs an interrupt-driven USB Serial/JTAG console and emits
  protocol-v1 `hello`, `node_snapshot`, and sequenced `heartbeat` envelopes.
- The host adapter ignores non-JSON boot diagnostics, strictly deserializes
  adapter envelopes, rejects unsupported protocol majors, and defaults to
  COM17.
- The UI advertises only device info, health, display status, touch input, and
  journal synchronization. Hardware-changing capabilities remain absent.
- The host adapter persists a 32-byte credential outside the repository,
  provisions it only while the device is unpaired over direct USB, verifies a
  mutually authenticated random challenge with domain-separated HMAC-SHA256,
  and forwards no device JSON before authentication succeeds.
- The firmware keeps an eight-entry NVS ring journal. The host acknowledges
  stable journal sequence numbers after forwarding them.

## Runtime-observed

- USB identity: Espressif `VID_303A&PID_1001`, USB Serial/JTAG, COM17.
- Chip: ESP32-P4 revision v1.3, dual HP cores plus LP core, 40 MHz crystal.
- Flash: 16 MiB. eFuse capability data and vendor documentation identify 32 MiB
  octal PSRAM.
- Security state: secure boot disabled, flash encryption disabled, JTAG enabled,
  and ROM download modes enabled. This unit is a development target, not a
  production trust anchor.
- The complete 16 MiB factory image was backed up before writing. Local recovery
  artifact: `.device-backups/tab5-30eda0e2e695-factory-20260718.bin`; SHA-256
  `9A57B3947FF63273435F34045730FEA58B76F01C1920453AC1EBB7FBE841CB8A`.
- The original image reported ESP-IDF 5.5.4 and used dual 6.25 MiB app slots,
  SPIFFS, NVS, OTA metadata, and a coredump partition.
- The compatible OrcLink bootloader, partition table, and native UI application
  were written to COM17. Esptool verified every written region by hash.
- The application emitted authenticated `hello`, `node_snapshot`, journal, and
  heartbeat envelopes under stable identity `m5tab5_30eda0e2e695`.
- A deliberately invalid host proof returned `AUTH_DENIED`; a subsequent
  unauthenticated `PING` produced zero JSON envelopes.
- Runtime snapshot: 1280x720 display, `touch_ready=true`, 16 MiB flash, 32 MiB
  PSRAM, and 422,680 bytes free heap after authentication.
- Physical touches produced mapped coordinates `(773,370)`, `(802,342)`,
  `(758,323)`, and `(724,358)` and were journaled with stable sequences 4-7.
- After the host adapter was stopped for longer than the five-second lease, the
  device retained journal record 8 (`session_degraded`). Reusing the same
  credential replayed record 8 followed by record 9 (`session_online`), and
  acknowledgements reduced pending records to zero.
- An app-only rewrite/reboot preserved both NVS identity and journal state.
  Reauthentication continued at record 10 (`boot`) and record 11
  (`session_online`), then cleared the pending queue.
- Built-in JTAG enumerates and can halt both HP cores. Some USB/JTAG reset-line
  combinations still enter ROM download mode (`boot 0x204`); normal verified
  esptool hard reset and direct serial reopen both booted the application.

## Authoritative platform facts

M5Stack documents the Tab5 with an ESP32-P4 main controller, ESP32-C6-MINI-1U
wireless coprocessor, 16 MiB flash, 32 MiB octal PSRAM, and a 5-inch 1280x720
touch display. The board also exposes USB host/OTG, microSD, RS485, audio,
camera, IMU, RTC, and battery interfaces. See the
[Tab5 product documentation](https://docs.m5stack.com/en/core/Tab5) and the
[official user demo](https://github.com/m5stack/M5Tab5-UserDemo) pinned during
validation at commit `68b19d37fbf9cefd5f256992f5dca34794c62ab4`.

## Gate result

The exact board, recovery path, stable identity, native touch UI, one telemetry
source, physical-USB authenticated session, bounded journal, degraded state,
host-loss replay, and app-reboot persistence are proven on hardware. The native
UI choice is therefore evidence-backed.

Full Phase 2 remains open on four explicit gates:

- ESP32-C6 Wi-Fi association and TLS/certificate validation need network
  credentials and AP/daemon loss testing.
- The development unit's NVS key is not protected because secure boot and flash
  encryption are disabled. Burning irreversible eFuses requires a separate
  production-hardening decision and is not part of this proof.
- Sudden-power-loss, storage-full, wear, and journal-pressure recovery remain
  unmeasured.
- Power, thermal, battery, sleep/wake, and vendor recovery measurements remain
  unmeasured.
