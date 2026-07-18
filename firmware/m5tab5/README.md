# M5Tab5 OrcLink agent

This is the smallest ESP-IDF proof for the M5Stack Tab5's ESP32-P4. It emits
OrcLink protocol-v1 `hello`, `node_snapshot`, and recurring `heartbeat` JSON
envelopes over the built-in USB Serial/JTAG console. It intentionally exposes
only read-only device information and health capabilities.

## Build and flash

The checked-in configuration targets pre-v3 ESP32-P4 silicon, matching
M5Stack's official Tab5 project and the measured v1.3 test unit.

```powershell
. 'C:\Espressif\Initialize-Idf.ps1' -IdfId esp-idf-5.5.3
idf.py build
idf.py -p COM17 flash
```

Do not use esptool's `--force` revision override. Preserve and hash a complete
flash backup before writing a device. Generated `build/` and `sdkconfig` files
are ignored; edit `sdkconfig.defaults` instead.

On the tested unit, a USB/JTAG reset samples download mode while the USB boot
strap is asserted. A physical power-cycle or reset-button action is therefore
required before the flashed application can be verified live.

The host reader defaults to COM17 and can be given another port explicitly:

```powershell
cargo run -p orclink-adapter-m5tab5 -- COM17
```
