# OrcLink — Orc Ecosystem Contract

> OrcLink is a sibling product in the Orc warband, not a renamed copy of TheOrc and not a second HIVE boss.

---

## Product Map

| Product | Warband role | Owns | Does not own |
|---|---|---|---|
| [TheOrc](https://github.com/hardcoreerik/TheOrc) | Warboss and local AI workbench | Goals, plans, agents, operator approvals, HIVE coordination, training, and source-grounded memory | Raw device transports or silent physical authority |
| **OrcLink** | Edge-control authority | Stable node identity, capability catalog, policy admission, device ownership, operation lifecycle, offline bounds, and Chronicle evidence | Open-ended AI planning or arbitrary shell execution |
| [TheOrc Companion](https://github.com/hardcoreerik/TheOrcCompanion) | Scout, relay, and field terminal | Route discovery, mobile visibility, constrained edge work, and field interaction | A second boss or unrestricted device control |

The names describe responsibility. “Warboss” is an orchestration role, “authority” is a policy and lifecycle role, and “scout” is a constrained edge role. None of those names grants permission by itself.

---

## Shared Product Principles

Every Orc project should preserve these promises:

1. **Local-first, cloud-optional.** Core operation remains useful on infrastructure the operator controls. Network dependencies are explicit.
2. **Human-owned authority.** AI may propose and explain; consequential authority comes from policy and the operator.
3. **Truth before theater.** Measured behavior, simulated behavior, and planned behavior are labeled separately.
4. **Narrow capabilities.** Tools declare bounded operations instead of exposing generic execution channels.
5. **Visible failure.** Offline, degraded, rejected, uncertain, and unverified are legitimate states—not UI problems to hide.
6. **Evidence survives the interface.** Closing a dashboard does not rewrite history or erase daemon-owned lifecycle facts.
7. **Sibling visual language.** Neon green on black, circuit frames, octagonal emblems, and warband vocabulary connect the products while each product keeps its own job.

---

## Integration Boundary

```text
operator
   │
   ▼
TheOrc ── typed proposal ──► OrcLink ── authorized capability ──► adapter / ESP32
   ▲                              │                                  │
   │                              └──────── Chronicle evidence ◄─────┘
   │
Companion ── route truth, field visibility, constrained edge work
```

TheOrc and other clients submit typed intent. OrcLink independently checks the requester, target, declared capability, fixed risk floor, approval requirements, deadline, and current availability. Adapters and ESP32 agents own transport details, but they do not expand authority.

The Companion can eventually surface OrcLink state and contribute constrained work, but it must use the same authenticated capability boundary. Mobile UI state is not authorization.

---

## Current Integration Status

| Integration | Status | Evidence |
|---|---|---|
| Shared visual assets and product language | ✅ Implemented | Exact TheOrc asset copies are provenance-recorded in `apps/control-room/ui/assets/theorc/README.md` |
| Typed TheOrc proposal schema | ✅ Implemented | `TheOrcProposal` is defined in `orclink-protocol` and admitted by the versioned local API |
| Rust SDK, CLI, and Control Room proposal clients | ✅ Implemented | `orclink-client`, `orclinkctl`, and Control Room use daemon-owned API state |
| Signed TheOrc requester enrollment and dispatch | 🔲 Planned | Proposal intake deliberately stops before authorization and adapter dispatch |
| Companion enrollment and OrcLink field view | 🔲 Planned | Product roles align, but no direct authenticated integration is claimed |
| Shared HIVE/OrcLink identity federation | 🔲 Planned | Requires an explicit trust and key-lifecycle design |

---

## Naming Rules

- Use **TheOrc** for the desktop orchestrator and HIVE boss.
- Use **OrcLink** for the edge-control system, daemon, protocol, and physical authority.
- Use **TheOrc Companion** for the Android/mobile field node.
- Use **Control Room** for OrcLink's operator client; it is not the daemon.
- Use **Chronicle** for OrcLink's append-only lifecycle evidence projection.
- Do not call a proposal an authorization, or admission an execution.

---

## Governance Links

- [Architecture](ARCHITECTURE.md)
- [Security and safety model](SECURITY_AND_SAFETY.md)
- [Local API](LOCAL_API.md)
- [Project status](../PROJECT_STATUS.md)
- [Security reporting policy](../SECURITY.md)
- [AI development disclosure](AI_DEVELOPMENT_DISCLOSURE.md)

OrcLink does not yet publish a software license. The sibling repositories currently use different terms, so ecosystem alignment does not imply a licensing choice.
