# OrcLink local API

OrcLink exposes a versioned, loopback-first HTTP API from `orclinkd`. Apps,
operator tools, and TheOrc use this surface instead of opening USB devices,
serial ports, adapter databases, or hardware handles directly.

Default endpoint: `http://127.0.0.1:4017`

The daemon remains the authority. Receiving a proposal does not authorize or
execute it. The v1 mutation surface admits typed intent into validation,
policy, approval, lifecycle, and Chronicle records.

## Versioned routes

| Method | Route | Purpose |
|---|---|---|
| `GET` | `/api/v1/status` | API, daemon, protocol, uptime, and registry counts |
| `GET` | `/api/v1/nodes` | Latest projection for every observed node |
| `GET` | `/api/v1/nodes/{node_id}` | Latest projection for one node |
| `GET` | `/api/v1/capabilities` | Observed/online targets, schema versions, fixed risk floors, and invokability |
| `GET` | `/api/v1/operations` | Accepted command/proposal lifecycle records |
| `GET` | `/api/v1/timeline?after=0&limit=200` | Bounded Chronicle facts; maximum limit is 1,000 |
| `POST` | `/api/v1/proposals` | Submit a strict `TheOrcProposal` envelope |
| `GET` | `/api/v1/proposals/{proposal_id}` | Read one persisted proposal receipt |
| `GET` | `/api/v1/openapi.json` | Machine-readable OpenAPI discovery document |

The original `/api/health`, `/api/snapshot`, `/api/operations`, and
`/api/timeline` reads remain available for existing Control Room clients.

## Proposal contract

```json
{
  "schema_version": 1,
  "proposal_id": "app:inventory:01",
  "requester_id": "workbench.app",
  "target": {
    "node_id": "host.windows.local",
    "component_id": null
  },
  "capability": {
    "name": "host.inventory.read",
    "schema_version": 1
  },
  "arguments": {
    "intent": "Refresh the bounded host inventory"
  },
  "risk": "R0",
  "approval_required": false,
  "idempotency_key": "app:inventory:01"
}
```

Admission verifies:

- proposal, requester, target, capability, and idempotency identifiers;
- supported proposal and capability schema versions;
- current target presence in the daemon registry;
- current target connection state is `online`;
- capability presence in the target's latest declaration;
- a daemon-owned fixed R0-R3 risk floor;
- requester risk is not below that floor;
- R1-R3 proposals are forced through human approval;
- direct password, token, secret, credential, PSK, and key fields are rejected;
- duplicate idempotency keys return the original receipt without another fact.

Valid R0 requests become `Proposed` for policy evaluation. Higher-risk requests
become `ApprovalRequired`. Neither state means dispatched or executed. Rejected
requests are recorded without persisting direct secret values.

## Rust SDK

`crates/orclink-client` is the reusable Rust client. It is deliberately limited
to loopback HTTP URLs in v0.1.

```rust
use orclink_client::Client;

let client = Client::new("http://127.0.0.1:4017")?;
let status = client.status()?;
let nodes = client.nodes()?;
let capabilities = client.capabilities()?;
```

The SDK exposes typed status, capability, timeline, and proposal-receipt
records while retaining adapter envelopes for namespaced node payloads.

## Operator CLI

Build the workspace, start `orclinkd`, then use:

```powershell
cargo run -p orclinkctl -- status
cargo run -p orclinkctl -- doctor
cargo run -p orclinkctl -- nodes
cargo run -p orclinkctl -- capabilities
cargo run -p orclinkctl -- operations
cargo run -p orclinkctl -- timeline 100
cargo run -p orclinkctl -- watch 30
cargo run -p orclinkctl -- propose .\examples\proposals\host-inventory.json
cargo run -p orclinkctl -- openapi
```

Use `--json` for machine-readable output and `--url` or `ORCLINK_URL` to select
another loopback port.

## Browser boundary

Native clients do not send a browser origin. Browser requests are accepted only
from loopback localhost/127.0.0.1 origins or Tauri's local origins. Foreign web
origins receive `403 ORIGIN_DENIED`. Responses disable caching and MIME sniffing.

This is a local API, not a remote-access design. Network listeners, TLS, device
mutual authentication, grants, approval signatures, and remote revocation need
their own measured security gate before the bind address expands beyond loopback.
