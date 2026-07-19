# ESP32 firmware-test workflow

Date: 2026-07-18

## Scope

The first Phase 3 workflow is deliberately restricted to the measured M5Tab5
on COM17 and the app-only image described by
`firmware/m5tab5/approved-ui.json`. COM5-COM8 are Bluetooth serial links; no
second USB ESP target is connected. This is not a generic flasher.

Before writing, the workflow requires all of the following to match:

- the manifest SHA-256 and the explicit `--approve` SHA-256;
- USB VID/PID `303a:1001` on the requested COM port;
- ESP32-P4 MAC `30:ed:a0:e2:e6:95`;
- app offset `0x10000`, QIO/80 MHz, and 16 MiB flash settings.

The authenticated M5Tab5 session receives the approved digest first, displays
`Approved firmware update starting`, journals `firmware_approved`, and echoes
the digest. Host approval remains authoritative; the display cannot broaden it.

After esptool verifies the write and resets the board, the workflow runs the
adapter once, verifies node `m5tab5_30eda0e2e695` and firmware `0.4.1`, waits
for journal acknowledgements, and rejects boot output containing watchdog,
panic, abort, or Guru Meditation markers. Every gate is appended to an ignored
`.orclink/firmware-test-<timestamp>.jsonl` audit.

## Run

Build the UI artifact first, then pass the exact digest from the manifest:

```powershell
cargo build -p orclink-adapter-m5tab5 -p orclink-firmware-test
target\debug\orclink-firmware-test.exe `
  firmware\m5tab5\approved-ui.json `
  firmware\m5tab5\.pio\build\m5tab5_ui\firmware.bin `
  COM17 --approve 62F04BA61C27C122DF77B53BE15A8AB2B2AAA805EF74992C71A1040A4C3EF3BC
```

## Live result

The bad-approval check exited before serial discovery. The approved hardware
run completed these audit stages: `artifact_hash`, `approval`,
`serial_discovery`, `device_update_display`, `stable_identity`,
`target_binding`, `flash`, and `reboot_verify`.
The verified 0.4.1 run is recorded at
`.orclink/firmware-test-1784435099.jsonl`. Its post-settle snapshots reported
ESP-Hosted station readiness and ten visible networks without joining one.

Skipped: bootloader/partition mutation, arbitrary artifacts, auto-approval,
and a second peripheral bridge. Add them only for a separately identified,
backed-up, approved target.
