# BrightSign integration

Date: 2026-07-19

The first BrightSign slice is read-only. It calls the official
[local Diagnostic Web Server info endpoint](https://docs.brightsign.biz/developers/ldws-info-endpoints)
`GET /api/v1/info/` and emits protocol-v1 hello and node
snapshot records containing only player serial, model, firmware, uptime,
connection type, and `brightsign.status.read`.

Current BrightSignOS releases disable local DWS access by default. When an
operator has explicitly enabled it, digest credentials are supplied through
`ORCLINK_BRIGHTSIGN_USERNAME` and `ORCLINK_BRIGHTSIGN_PASSWORD`; they are not
accepted in the endpoint URL or placed on a process command line.

```powershell
$env:ORCLINK_BRIGHTSIGN_USERNAME='operator'
$env:ORCLINK_BRIGHTSIGN_PASSWORD='use-a-secret-source'
cargo run -p orclink-adapter-brightsign -- http://player-address
```

The adapter contains no reboot, content upload, presentation control,
deployment, storage mutation, or configuration endpoint. Those operations need
an identified test player, native safeguards, approval, reconciliation, and
post-operation verification before being added individually.

## Evidence

A local fake-player test verified the exact `/api/v1/info/` request, nested DWS
response handling, safe-field projection, and omission of unrequested network
details. Unit tests and clippy pass. A bounded probe to `127.0.0.1` was actively
refused, and no BrightSign USB identity, cached hostname, or BrightSign OUI was
present. Live player identity, DWS authentication, and status therefore remain
unverified.
