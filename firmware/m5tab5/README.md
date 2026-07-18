# M5Tab5 OrcLink agent

This directory contains two narrow M5Stack Tab5 proofs:

- The ESP-IDF agent emits OrcLink protocol-v1 `hello`, `node_snapshot`, and
  recurring `heartbeat` envelopes over USB Serial/JTAG.
- The `m5tab5_ui` PlatformIO environment adds the 1280x720 display and GT911
  touch path using the M5Unified/M5GFX initialization proven in NEONDRIVE.

The UI build deliberately leaves display and touch autodetection to
`M5.begin()`, calls `M5.update()` once per loop, and assigns display rotation
only once. Do not add manual GT911 or PI4IOE reset sequences.

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

## Display and touch proof

The UI environment pins pioarduino 55.03.38-1, Arduino 3.3.8, M5Unified
0.2.15, and M5GFX 0.2.21. Its pre-build patch preserves the I2C bus across
M5GFX autodetection on ESP-IDF 5.5.

```powershell
pio run -e m5tab5_ui
pio run -e m5tab5_ui -t upload --upload-port COM17
```

PlatformIO must run on Python 3.10 through 3.13 for this pioarduino release.
After flashing, reset the device if it remains in download mode. A successful
touch on the on-screen button emits an `operator_touch` protocol event with
the mapped display coordinates.

Run the host adapter to provision or reuse the ignored local credential,
authenticate the device, forward envelopes, and acknowledge journal records:

```powershell
cargo run -p orclink-adapter-m5tab5 -- COM17
```

The development unit stores its device credential in unencrypted NVS. This is
adequate for the direct-USB proof, not a production protected-key claim.
