# 0003. Track Chromium Extended Stable

- Status: Accepted
- Date: 2026-09-28

## Context

Each Chromium milestone we move to means a rebase of the patch series, a full rebuild, the full test suite, and a release. A milestone rebase costs one to three engineer-days for a small series and grows with it. Upstream security fixes arrive within a milestone as new patch releases (`MAJOR.0.BUILD.PATCH`). Moving to a new patch release of the same milestone is cheap, because our series rarely conflicts with security fixes.

Release history from chromiumdash (Windows, first release of each milestone), retrieved 2026-09-28:

| Channel | Milestones and first release date |
|---|---|
| Stable | M150 2026-06-17, M151 2026-07-15, M152 2026-08-12, **M153 2026-08-26, M154 2026-09-09, M155 2026-09-23** |
| Extended Stable | M146 2026-03-10, M148 2026-05-05, M150 2026-06-30, M152 2026-08-25 |

Since M152, Stable ships a new major every **two weeks** (M156 is scheduled for 2026-10-07). Extended Stable still moves to every second milestone, roughly every eight weeks, and keeps receiving security releases. The latest Windows Extended release at decision time was **152.0.7977.140** (2026-09-22).

Following Stable would mean about 26 milestone rebases a year. Extended Stable means about 6–7.

## Decision

- Track Chromium Extended Stable. `CHROMIUM_VERSION` always names an Extended Stable release tag.
- Move to each new security release of the current milestone within the SLA in [docs/roadmap.md](../roadmap.md#security-release-sla).
- Move to the next Extended Stable milestone when Google promotes it.
- Run a nightly **canary rebase** of the series onto the current Beta tag. This isn't a release; it tells us which patches will conflict before a milestone move is due.
- Revisit this decision once two milestone moves have been measured, or if Google changes the Extended Stable programme.

## Consequences

- **Rebase work drops by about 4x** compared with following Stable.
- **Our version lags Stable by up to two months of features, and by several milestone numbers** (at decision time: 152 against 155). The reduced User-Agent exposes the major version. So we look like the Extended Stable population, mostly managed enterprise installs, rather than the Stable majority.
  - Reporting a version we don't implement wouldn't help: feature detection reveals the engine version regardless.
  - This is a known fingerprinting cost, recorded in [docs/privacy-model.md](../privacy-model.md#limitations).
- **Some security fixes may reach Extended Stable after Stable.** When a fix is public, rated High or Critical, and not yet on Extended, we cherry-pick it rather than wait.
- **Web platform features arrive later** for our users and extension authors.

## Alternatives considered

- **Track Stable.** Best web compatibility and the largest user crowd. At a two-week cadence it's unaffordable for a small team: we would spend most engineering time re-basing.
- **Track Stable, but only every Nth milestone.** Leaves users on milestones that Google has stopped patching. Not acceptable.
- **Pin a milestone and backport security fixes ourselves.** Backporting across diverging code is where downstream browsers historically fall behind on security.
