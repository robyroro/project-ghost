# 0002. MPL-2.0 for project code, DCO for contributions

- Status: Accepted
- Date: 2026-09-28

## Context

The browser core is open source. The project may later sell optional services, such as encrypted sync, routing infrastructure, and team identity management. The license must:

- keep improvements to the privacy engine public, even when a company builds a closed product on it;
- combine cleanly with Chromium (BSD-3-Clause) and adblock-rust (MPL-2.0);
- not stop us from keeping service clients or servers in separate repositories under their own licenses.

## Decision

- **Code in this repository is licensed under MPL-2.0.** Every source file we write carries the MPL-2.0 Exhibit A notice, which `tools/lint.py` enforces.
- **Contributions are accepted under the Developer Certificate of Origin** (`Signed-off-by`), not a CLA.
- **Server-side services live in separate repositories**, licensed per service.

## Consequences

- MPL-2.0 is copyleft per file. Anyone shipping a modified version of our files must publish those modifications. They can still combine our files with differently licensed files in a larger work, which is how our code sits inside Chromium.
- MPL-2.0 is compatible with the GPL through its secondary-license clause. It lets us use MPL and BSD dependencies freely.
- The DCO keeps contribution friction low. It gives the project no right to relicense contributions. We accept that: the core should stay MPL-2.0 permanently.
- Copying from GPL-licensed projects (uBlock Origin, Cromite) into our files is not possible. See [docs/licensing.md](../licensing.md).

## Alternatives considered

- **BSD-3-Clause, matching Chromium.** Maximally permissive. It would let a competitor ship closed modifications of our privacy engine, which undercuts the transparency argument that is central to the project.
- **GPL-3.0 / AGPL-3.0.** Stronger copyleft. It would make combining with BSD-licensed Chromium files awkward, prevent MPL-style file-level mixing, and deter commercial adopters of individual components.
- **CLA.** Allows relicensing and dual-licensing, but deters contributors and signals that relicensing is planned. It isn't.
