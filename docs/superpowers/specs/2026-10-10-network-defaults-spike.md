# Network defaults: progress notes

- Phase 3, sub-project 3C ([design](2026-10-10-network-defaults-design.md), [plan](../plans/2026-10-10-network-defaults.md))
- Machine: the reference machine (Ryzen 5 3600, 6 cores, 32 GB)
- Chromium 152.0.7977.158, the series unchanged (32 patches); `out/vanilla`

## What changed in Shade

| Default | Where |
|---|---|
| Global Privacy Control on (`blink::features::kGlobalPrivacyControlForce`) | `browser/features/feature_overrides.cc` |
| WebRTC's IP-handling policy `default_public_interface_only` | `browser/prefs/pref_defaults.cc` |
| Strict HTTPS-First in Incognito (`https_only_mode_enabled`, in each Incognito profile's memory) | `browser/startup/incognito_defaults.{h,cc}`, from `BrowserMainExtraParts::PostProfileInit` |

No new patch for these: patches 0002, 0003 and 0005's hooks reach all three. One patch for a finding below: **0033**, no ETW log provider (33 patches).

## Where the design was wrong

| The design said | What the sources and tests showed |
|---|---|
| Strict HTTPS-First in Incognito is already upstream (`https_first_mode_incognito_enabled`, `kHttpsFirstModeIncognito`). | **No.** `enabled_by_incognito` turns on the same warnings as balanced mode, which Shade's Normal already has; only `https_only_mode_enabled` and Advanced Protection are strict (`IsStrictInterstitialEnabled`). The test showed it before the cause was read: on an HTTP-only `.test` host Incognito showed no warning. Strict differs from balanced on hosts that aren't unique (intranet names, `.test`): strict warns, balanced lets them load (`ShouldExemptNonUniqueHostnames`). Fixed by `IncognitoDefaults`, which sets the pref in each Incognito profile as it is created; the pref is not on Incognito's write-through list (`pref_service_incognito_allowlist.cc`), so it stays in memory and Normal stays balanced (the test checks both). |
| Related Website Sets need a default of their own. | **No.** Upstream turns them off on a profile's first run when third-party cookies are blocked (`PrivacySandboxServiceImpl::MaybeInitializeRelatedWebsiteSetsPref`), which Shade's default does. A default of their own would have changed nothing observable, and its mutation check couldn't have failed. The test proves them off; mutation check M3 (third-party cookies at upstream's default) turns them back on. |
| `document.browsingTopics()` rejects in a page. | It rejects in a test page whatever the settings (the page isn't attested), so it can't fail: the test asks `PrivacySandboxSettings` instead. |
| `IsAttributionReportingEverAllowed()` | Not in `PrivacySandboxSettings` at 152; ad measurement's decision is the pref `privacy_sandbox.m1.ad_measurement_enabled`. |

## Settled on the way

- A first version of the HTTPS-First test used a `.com` host whose HTTPS upgrade fails with an SSL error: Incognito warned, but so did Normal (balanced warns on that failure). Its control made it fail, and the test moved to the case that tells the two modes apart.
- Page hints (`<link rel=preconnect>`, `rel=dns-prefetch`) reach `PreconnectManagerImpl`, which checks the preloading pref before connecting and before resolving. A connection is observable, a resolution isn't: the test counts connections, and the privacy model says DNS prefetch passes the same check. Under mutation check M4 the second server accepted one connection.
- `PostProfileInit` runs for every profile, those created later included (`chrome_browser_main.cc`), so a profile added after startup gets the same Incognito defaults.

## Found: Windows telemetry received the browser's log messages

The browser tests that open an Incognito window failed twice in a row with `EXCESSIVE_OUTPUT` (500 KB of log). The log was VERBOSE1 lines and Blink property trees from the GPU and renderer processes, printed only when `VLOG_IS_ON(1)`; no switch asked for it (`--vmodule=*=0` silenced it, `--v=0` didn't). The cause was outside Chromium: a Windows telemetry session, `WPR_initiated_DiagTrackMiniLogger_OneTrace_User_Logger_20261005_1_EC_0`, had enabled Chromium's ETW log provider `{7FE69228-633E-4F06-80C1-527FEA23E3A7}` at level 5 (verbose) with every keyword. When a session enables it, `LogEventProvider` lowers each process's minimum log level to the session's and sends it every message, and many messages carry URLs and host names.

Decided with the user: **patch 0033**, `InitChromeLogging` registers no provider. `EtwLoggingBrowserTest` checks the registration handle (it failed before the patch, with the handle set). Afterwards one test's output went from 1,588,309 bytes to 1,998, and the whole browser suite passes with no retry, the first time since Phase 1, whose `EXCESSIVE_OUTPUT` retries had the same cause. Not changed: the installer's and the updater's own providers (other GUIDs, which the session didn't enable).

## Mutation checks

Each made in `src/ghost`, built, seen failing, undone (`src/ghost` compared with webops afterwards):

| Check | Fails |
|---|---|
| M1: GPC not forced | `GpcIsSentOnNavigationsAndSubresources`, `GpcIsVisibleToPagesAndWorkers` |
| M2: WebRTC's policy back to `default` | `WebRtcUsesOnlyTheDefaultPublicInterface` |
| M3: third-party cookies at upstream's default (`kIncognitoOnly`) | `RelatedWebsiteSetsAreOff`, `ThirdPartyCookiesAreNotSent` |
| M4: network prediction at upstream's default | `PageHintsAndSpeculationRulesContactNobody` (one connection accepted) |
| M5: Incognito not strict | `IncognitoIsStrictAndNormalBalanced` |
| Patch 0033 absent (the build before it) | `TheBrowserRegistersNoEtwLogProvider` |

## End to end, 2026-10-10

| Check | Result |
|---|---|
| Tooling tests | pass (1 skipped: the TPM test) |
| `ghost_unittests` | 67 of 67 |
| `ghost_browsertests` | 47 of 47 (12 new), **no retry** |
| Egress audit, `out/vanilla` | 0 unexpected hosts (the route probe and `wpad`, not counted), before and after patch 0033 |

## Not covered yet

- The installer's and updater's ETW providers.
- GPC's switch (3D); WebRTC limited to proxied connections while a route is active (Phase 9); the Strict level's referrer (3D).
