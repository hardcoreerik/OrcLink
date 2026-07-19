# RTL-SDR and SatDump integration

Date: 2026-07-19

The first SDR slice is a bounded Windows adapter around `rtl_sdr` and SatDump.
It does not contain signal-processing code or choose a decoder pipeline.

Capture owns these controls:

- a named Windows mutex reserves RTL-SDR device 0 without stale lock files;
- the output volume is checked before capture;
- frequency, sample rate, byte bound, and optional Unix-millisecond start time
  are explicit;
- the adapter stops `rtl_sdr` itself, truncates to the exact CU8 byte bound,
  and publishes the artifact only after capture completes;
- a JSON sidecar records tuning, timing, observed USB serial/tuner, size, and
  SHA-256 identity.

```powershell
cargo run -p orclink-adapter-rtl-sdr -- capture <output.cu8> <frequency_hz> <sample_rate_hz> <max_bytes> [start_unix_ms]
```

Decoding first verifies the capture against its sidecar. It requires an
explicit SatDump pipeline and a new output directory, and enforces a decoder
timeout. A successful process exit is recorded as
`process_completed_signal_unverified`; it is not proof that a valid signal was
present.

```powershell
cargo run -p orclink-adapter-rtl-sdr -- decode <capture.cu8> <capture.cu8.json> <pipeline> <new_output_dir> [timeout_seconds]
```

## Measured proof

The official RTL-SDR.COM V4 connected directly to NEWCOREPC enumerated as
RTL2832U serial `77771111153705700` with an R828D tuner after WinUSB was
installed. OrcLink captured exactly 4,800,000 CU8 bytes at 100 MHz and
2.4 MS/s. The artifact SHA-256 was
`4C2EF64DF132BD8EA0CB64F412ECE9B0D32122157237BFBFF8D05B6FC9C71946`.

SatDump 1.2.2 completed its `noaa_apt` pipeline against that bounded artifact
and produced decoder outputs. This was intentionally a process-boundary smoke
test: 100 MHz was not a NOAA APT pass, SatDump reported that it could not
identify valid calibration wedges, and no received satellite signal is
claimed.

Bias-tee control, automatic gain, PPM calibration, pass prediction, antenna
validation, and successful over-the-air decoding remain outside this slice.
