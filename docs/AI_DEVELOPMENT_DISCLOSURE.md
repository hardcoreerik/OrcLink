# OrcLink — AI Development Disclosure

> OrcLink is human-directed and AI-assisted. AI output is treated as proposed work, not evidence that hardware or software behaves as claimed.

---

## Human Ownership

[Erik / hardcoreerik](https://github.com/hardcoreerik) owns product direction, architecture acceptance, hardware selection, physical testing, risk decisions, credentials, releases, and repository administration.

AI assistants may help inspect code, research public documentation, draft designs, implement changes, write tests, review failures, and prepare documentation. Their output enters the same review and verification path as any other untrusted proposal.

---

## Evidence Standard

OrcLink separates four kinds of claim:

| Label | Meaning |
|---|---|
| Implemented | Code exists and passes its stated software checks |
| Simulated | Behavior is proven only against deterministic fixtures or mocks |
| Measured | Behavior was observed on the identified hardware or live service |
| Planned | Design direction exists, but implementation or proof does not |

An AI-generated explanation, successful compilation, or passing mock is not a measured hardware result. Hardware claims are recorded only after operator-observed tests on the named target. Known gaps remain visible in `PROJECT_STATUS.md` and the integration documents.

---

## Review Discipline

- AI does not approve its own consequential hardware operations.
- Generated code is formatted, compiled, linted, and tested where the project provides those gates.
- Security-sensitive behavior is expected to fail closed and receive adversarial review.
- Documentation changes ship with implementation changes when behavior changes.
- Incorrect claims should be corrected publicly rather than preserved for appearance.
- Credentials, pairing material, private logs, and personal machine paths must not enter the repository.

---

## Current Tooling Disclosure

The public project has received implementation, review, documentation, and verification assistance from AI coding systems, including OpenAI Codex. Commit authorship alone should not be read as a complete record of which assistant contributed to an individual line.

The maintainer remains responsible for accepting every published change. To report a mismatch between documentation and behavior, open a normal issue. For a vulnerability, follow the confidential process in [SECURITY.md](../SECURITY.md).
