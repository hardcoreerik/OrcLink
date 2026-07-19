# Moonraker integration

Date: 2026-07-18

The first Klipper slice is read-only. It follows Moonraker's official HTTP API
for server information, printer information, loaded-object discovery, and
printer-object status queries:

- `GET /server/info`
- `GET /printer/info`
- `GET /printer/objects/list`
- `POST /printer/objects/query`

The adapter selects only loaded `extruder`, `heater_bed`, `print_stats`, and
`toolhead` objects, then emits protocol-v1 hello and node-snapshot records. It
contains no G-code, print start/cancel, restart, shutdown, or emergency-stop
endpoint. See the [official Moonraker printer API](https://moonraker.readthedocs.io/en/latest/external_api/printer/)
and [server API](https://moonraker.readthedocs.io/en/latest/external_api/server/).

Run it against an explicitly approved endpoint:

```powershell
cargo run -p orclink-adapter-moonraker -- http://printer-host:7125
```

The 2026-07-18 probe to the default local endpoint
`http://127.0.0.1:7125` returned connection refused. No network scan was run.
Live authentication, alerts, native printer safeguards, and all mutating
capabilities require an identified test printer and separate evidence.
