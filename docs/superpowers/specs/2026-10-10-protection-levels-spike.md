# Protection levels per site: progress notes

- Phase 3, sub-project 3D-1 ([design](2026-10-10-protection-levels-design.md), [plan](../plans/2026-10-10-protection-levels.md))
- Machine: the reference machine (Ryzen 5 3600, 6 cores, 32 GB)
- Chromium 152.0.7977.158, the series with patch 0029 amended (33 patches); `out/vanilla`
- The user decided what the levels mean, then asked Claude to decide the rest; each decision is in the design with its reason. Committed locally; the push waits for the user.

## The spike's questions

| Question | Answer |
|---|---|
| Can a navigation throttle drop the `Referer`? | Yes: clearing `request->referrer` with `NO_REFERRER` in `WillStartRequest` sends none (`AStrictPageNavigatesWithoutReferrer`). **But `document.referrer` on the destination still shows the origin** (`http://a.test:<port>/` in the test's log): the renderer takes it from the navigation's parameters, which a `URLLoaderThrottle` doesn't reach. The privacy model says so. |
| Is the top frame known for every request the filter sees? | The filter already resolves the page per request (`RequestFilter::SourceOf`: the factory's top frame, the request's trusted isolation info, or its initiator), so the level is resolved per request too, through a callback bound to `BrowserContext::GetWeakPtr()`; a profile that is gone gets Standard. |
| Does Incognito's pref overlay behave as decided? | Yes (`SiteLevelsTest`): Incognito reads the regular profile's choice until it writes, and its write doesn't reach the regular profile or its dictionary. |

## Found on the way

| Finding | What was done |
|---|---|
| `IncognitoIsStrictByDefault` passed **before** the implementation: 3C made Incognito's HTTPS-First strict, so an `http://a.test` page showed the warning instead of loading, and every script "failed". | The test runs on the HTTPS server (`CERT_TEST_NAMES`) with a control: a script that must load. It then failed until Strict applied, as it should. |
| Patch 0029 had to pass the `BrowserContext`. | Amended with a fixup and `rebase --autosquash`: only 0030–0033 were replayed (0032 changed only its context lines); the build took 2 minutes. |

## Mutation checks

Each made in `src/ghost`, built, seen failing, undone (`src/ghost` compared with webops afterwards):

| Check | Fails |
|---|---|
| M1: blocking ignores Off | `AnOffSiteLoadsTrackers`, `ALevelAppliesAtTheNextLoad` |
| M2: Strict doesn't check same-site requests | `StrictBlocksTheSitesOwnTrackers`, `IncognitoIsStrictByDefault` |
| M3: Strict keeps the cross-site referrer (the filter and the throttle) | `StrictSendsNoCrossSiteReferrer`, `AStrictPageNavigatesWithoutReferrer` |
| M4: Incognito defaults to Standard | `IncognitoIsStrictByDefault`, `QueryFilterBrowserTest.CampaignParametersAreStrippedInIncognito` |

## End to end, 2026-10-10

| Check | Result |
|---|---|
| Tooling tests | pass (1 skipped: the TPM test) |
| `ghost_unittests` | 79 of 79 (12 new) |
| `ghost_browsertests` | 58 of 58 (11 new), no retry |
| Egress audit, `out/vanilla` | 0 unexpected hosts (the route probe and `wpad`, not counted) |

## Not covered yet

- The controls: the panel (3D-3) and settings (3D-4).
- `document.referrer` on a Strict page's cross-site destination.
- WebSocket and WebTransport (3D-2).
