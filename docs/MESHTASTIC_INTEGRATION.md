# Meshtastic integration

Date: 2026-07-19

The first Meshtastic slice is a receive-only Python sidecar using the official
Meshtastic SDK. It deliberately contains no send, traceroute, configuration,
position-request, reboot, or GPIO operation.

The adapter requires both the USB serial and Meshtastic node ID. A changing COM
number alone cannot select a trusted node. Its JSONL output contains:

- protocol-v1 hello, node snapshot, packet-metadata event, and heartbeat
  envelopes;
- local firmware, hardware, region, and non-position health metrics;
- privacy-safe topology using node ID, hop distance, last-heard time, and SNR;
- bounded packet metadata with message bodies and decoded payloads removed.

Names are omitted unless `--include-names` is explicitly supplied. Positions,
public keys, MAC addresses, channel keys, packet text, and packet payload bytes
are never emitted. The adapter reports the radio's observed transmit setting
but exposes no transmit capability.

Install the pinned official SDK and run the paired node:

```powershell
python -m pip install -r adapters/meshtastic/requirements.txt
python adapters/meshtastic/readonly.py --port COM11 --expect-usb-serial 44:1B:F6:70:A2:24 --expect-node-id !f670a224 --listen-seconds 30
```

## Measured proof

COM11 is an Espressif USB JTAG/serial device with stable USB serial
`44:1B:F6:70:A2:24`. The official SDK identified node `!f670a224`, name
`Kushcore`, hardware `HELTEC_V4`, firmware `2.7.20.2be5225`, region `US`, and
53 known nodes. The privacy-default snapshot emitted all five available local
health fields and a 53-node topology without names or positions. A wrong node
ID was rejected after connection.

A 30-second bounded listener completed with zero received packets. The packet
sanitizer self-test proves allowed metadata survives while message text,
payload bytes, position, and key fields do not; actual over-the-air packet-event
ingestion remains unobserved until traffic arrives. No LoRa packet was sent and
no device setting was changed. The device itself currently reports transmit as
enabled, but OrcLink exposes read-only capabilities only.
