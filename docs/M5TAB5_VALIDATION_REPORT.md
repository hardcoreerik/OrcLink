# M5Tab5 validation report

Date: 2026-07-18

Target: M5Stack Tab5 on COM17

Status: build and flash verified; application boot proof awaits one physical reset

## Repo-verified

- The firmware builds with ESP-IDF 5.5.3 for `esp32p4` and pre-v3 silicon,
  minimum revision v0.1. That silicon family selection matches the official
  M5Stack Tab5 demo configuration.
- The application image is 222,208 bytes and has 86% of its 1,536 KiB app
  partition free.
- The firmware installs an interrupt-driven USB Serial/JTAG console and emits
  protocol-v1 `hello`, `node_snapshot`, and sequenced `heartbeat` envelopes.
- The host adapter ignores non-JSON boot diagnostics, strictly deserializes
  adapter envelopes, rejects unsupported protocol majors, and defaults to
  COM17.
- Hardware-changing capabilities remain absent. The advertised surface is
  limited to `m5tab5.device.info` and `m5tab5.health.read`.

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
- The compatible OrcLink bootloader, partition table, and application were
  written to COM17. Esptool verified every written region by hash.
- Built-in JTAG enumerates and can halt both HP cores. Remote USB/JTAG resets
  repeatedly enter ROM download mode (`boot 0x204`) because the USB boot strap
  remains asserted. No live application envelope has therefore been observed.
- Internal software-reset trials covered all four DTR/RTS combinations; every
  combination still entered `boot 0x204`. The device is behind two shared USB
  hubs, so cycling a parent hub would disrupt unrelated devices and would not
  reliably remove battery power.

## Authoritative platform facts

M5Stack documents the Tab5 with an ESP32-P4 main controller, ESP32-C6-MINI-1U
wireless coprocessor, 16 MiB flash, 32 MiB octal PSRAM, and a 5-inch 1280x720
touch display. The board also exposes USB host/OTG, microSD, RS485, audio,
camera, IMU, RTC, and battery interfaces. See the
[Tab5 product documentation](https://docs.m5stack.com/en/core/Tab5) and the
[official user demo](https://github.com/m5stack/M5Tab5-UserDemo) pinned during
validation at commit `68b19d37fbf9cefd5f256992f5dca34794c62ab4`.

## Gate result

The exact board, build target, recovery path, flash process, and host-side wire
reader are proven. Phase 2 is not complete: a person must power-cycle or press
reset once, after which the serial stream must be captured and schema-validated.
Display/touch, identity storage, authenticated transport, durable journal,
degraded UI, and reconnect tests remain unproven and disabled.
