# Architecture decision records

An ADR records one decision that shapes the codebase: the context at the time, what we decided, and what it costs us. ADRs are never rewritten after acceptance. When a decision changes, a new ADR supersedes the old one and both stay in the log.

Write an ADR for anything that is expensive to reverse:
- choice of engine, library or protocol;
- a new process, service or trust boundary;
- a change to what we promise users;
- a new category of patch against Chromium.

| ADR | Title | Status |
|---|---|---|
| [0001](0001-engine-chromium.md) | Build on Chromium | Accepted |
| [0002](0002-license-mpl2-dco.md) | MPL-2.0 for project code, DCO for contributions | Accepted |
| [0003](0003-upstream-extended-stable.md) | Track Chromium Extended Stable | Accepted |
| [0004](0004-patch-strategy.md) | Hook-first changes and a git-native patch series | Accepted |
| [0005](0005-isolation-primitives.md) | Storage partitions for identities, off-the-record profiles for Ghost sessions | Accepted |
| [0006](0006-blocking-engine-adblock-rust.md) | adblock-rust as the content-blocking engine | Accepted |
| [0007](0007-version-numbers.md) | Version numbers: Chromium's, with Ghost releases in the fourth part | Proposed |

## Template

```markdown
# NNNN. Title

- Status: Proposed | Accepted | Superseded by NNNN
- Date: YYYY-MM-DD

## Context
What forces are at play, including facts we measured.

## Decision
What we will do, stated so that a reviewer can check code against it.

## Consequences
What becomes easier, what becomes harder, what we must now maintain.

## Alternatives considered
Each serious alternative and the specific reason it lost.
```
