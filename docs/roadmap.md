# Roadmap

**Current phase: 1 (browser shell).** Phase 0 is complete.

The phases run in an order chosen to retire the largest risks first. Before any privacy feature is built, we prove that we can build, ship and update Chromium ourselves (Phases 1–2). Keeping up with upstream security releases is the thing most likely to fail for a small team.

Estimates are in engineer-weeks and assume engineers who already know Chromium. Add 50–100% for ramp-up. They'll be recalibrated with measured build and rebase times after Phase 1. From Phase 2 on, upstream maintenance runs alongside feature work and consumes roughly half to one engineer permanently.

## Phases to public alpha

| Phase | Scope | Exit criteria | Estimate |
|---|---|---|---|
| **0. Foundations** | Engine decision, architecture and policy documents, ADRs, repository, environment check, bootstrap, patch tooling, tooling CI, Chromium pin | Tooling tests and lint green in CI; documents reviewed | 2–4 |
| **1. Browser shell** | Baseline Chromium build at the pin; branding; Google services removed; privacy-preserving defaults; egress audit; Windows installer | See [Phase 1](#phase-1-browser-shell) | 4–8 |
| **2. Release engineering** | Branded `//chrome/updater`; update and component server; code signing; first real move to a new security release and a new milestone; security response runbook | An update shipped end to end to test machines; one milestone move measured | 6–10 |
| **3. Network protections** | Blocking engine and lists; tracking-parameter stripping; third-party cookie, HTTPS, DNS, referrer, WebRTC and GPC defaults; minimal protections panel with per-site levels | Blocking, parameter and egress tests green; blocking overhead within budget | 8–12 |
| **4. Content protections** | Cosmetic filtering, scriptlets, CNAME uncloaking, breakage controls | Compatibility corpus pass rate no worse than vanilla Chromium with blocking off | 6–10 |
| **5. Private and Ghost sessions** | Hardened Private defaults; unique-OTR Ghost sessions; Ghost window treatment; audit of Incognito-specific UI | Isolation contract rows for sessions green; destruction and file-system tests green | 6–10 |
| **6. Identities** | Per-tab storage partitions; per-identity permissions; domain rules; session restore; identity UI | Isolation contract rows for identities green, including "shared by design" rows | 12–20 |
| **7. Fingerprinting** | Blink supplements; standardization; keyed noise; worker coverage; per-mode tiers | Consistency, determinism and unlinkability tests green across frames, workers and media queries | 10–16 |
| **8. Privacy report** | Event accounting; versioned score; report UI; "Harden this site" | Score fixtures reproduce exactly; every deduction explained in UI | 4–6 |
| **9. Routing** | HTTP and SOCKS5 routes per identity and session; fail-closed behavior; WebRTC, DNS and QUIC coupling; SOCKS5 authentication | Leak tests (DNS, WebRTC, QUIC, prefetch) green with routes active | 6–10 |
| **Public alpha** | Release gates cleared ([licensing.md](licensing.md#release-gates)); final name; public documentation | — | — |

**After the public alpha:**
- encrypted sync, reusing Chromium's sync engine with an always-on custom passphrase against our own server;
- macOS and Linux builds;
- public beta.

## MVP: the public alpha

**In scope**
- A signed Windows 11 x64 installer with auto-update and the security SLA below.
- No Google services, and an egress audit that proves it.
- Built-in blocking, including cosmetic filtering and scriptlets.
- Parameter stripping, third-party cookie blocking, HTTPS-First, DNS-over-HTTPS and GPC.
- Private mode and Ghost sessions.
- Persistent identities with isolated storage, network state, powerful-permission grants and routes, plus domain rules.
- Fingerprinting tiers per [privacy-model.md](privacy-model.md#fingerprinting).
- The protections panel and privacy report.
- HTTP and SOCKS5 routes.
- Chrome Web Store extensions.
- No telemetry; crash upload off.

**Out of scope**
- Sync, VPN, Tor, macOS and Linux.
- Any feature needing a project-operated service.

## Not built yet

These are deliberate exclusions, not oversights:

- **Tor routing.** It would come later, only in Ghost sessions, via an embedded client exposed as SOCKS5, and clearly labelled as not Tor Browser.
- **VPN and sync.** Both need service infrastructure and threat modelling of their own.
- **Rewards, crypto wallets, AI features.** None belongs in a privacy browser's core. Any rewards integration would be an optional, separately installed extension.
- **Telemetry of any kind.**
- **Fingerprint personas and user-set spoofed values.** They increase uniqueness.
- **Reviving Manifest V2.** Our blocking doesn't need it, and carrying it would be a permanent patch burden.
- **Per-identity extension sets and letterboxing.** Candidates after the alpha.
- **Mobile, our own search engine, and large UI features** such as vertical tabs and workspaces.

## Security release SLA

From Phase 2 on:

| Upstream event | We ship within |
|---|---|
| Chromium security release for our milestone (Extended Stable) | 72 hours |
| Chromium release fixing a vulnerability exploited in the wild | 24–48 hours |
| High or Critical fix released on Stable but not yet on Extended Stable | Cherry-picked into the next release (see [ADR 0003](adr/0003-upstream-extended-stable.md)) |

Meeting this requires:
- an automated release pipeline;
- a build host able to produce a signed release from a tag without manual steps;
- an on-call rotation.

All three are Phase 2 exit requirements.

## Phase 0: foundations

Delivered in this repository:

- [x] Engine decision and ADRs 0001–0006.
- [x] Architecture, privacy model, threat model, licensing, roadmap, testing, patching and Windows build documents.
- [x] License, contribution rules, security policy, code of conduct.
- [x] `CHROMIUM_VERSION` pinned to 152.0.7977.140 (Windows Extended Stable, 2026-09-22), with its toolchain requirements in `build/requirements.json`.
- [x] `tools/check_env.py`, `tools/bootstrap.py`, `tools/patches.py`, `tools/lint.py` with tests.
- [x] Tooling CI workflow.

Remaining before Phase 0 closes:

- [x] The first CI run on GitHub: 85 tooling tests and lint green on Windows and Ubuntu (2026-09-29).
- [x] A conduct contact address in `CODE_OF_CONDUCT.md` (2026-09-30).

## Phase 1: browser shell

**Prerequisites** (performed by the developer; `tools/check_env.py` verifies them):
- a build volume without spaces in the path and at least 250 GB free, ideally a Dev Drive;
- Visual Studio 2022 or 2026 with the C++ toolset and ATL/MFC;
- Windows SDK 10.0.26100.7705 with Debugging Tools;
- `DEPOT_TOOLS_WIN_TOOLCHAIN=0`;
- the git settings listed by `check_env.py`;
- a Microsoft Defender exclusion for the build root.

**Steps**
1. `tools/check_env.py --build-root <root>` passes. `tools/bootstrap.py --root <root>` creates the checkout, or adopts an existing one.
2. **Baseline build.** Vanilla Chromium at the pin (component build, reduced symbols). Record build time and disk use in [build/windows.md](build/windows.md). This build is the reference for performance and compatibility comparisons.
3. **Hook patches.** Add `//ghost` to `chrome`, and the test targets to `gn_all`. Add `build/args/{dev,release}.gn`, verified with `gn gen` and `gn check`.
4. **Branding.** Create `branding/`: product strings, icons, newly generated application IDs, AppUserModelID, ProgID, and a development user-data directory. Selected by a branding hook patch. Verified on the About page, in file properties and in the registry after installation.
5. **Defaults.**
   - `SetDefaultPrefValue` overrides: third-party cookies blocked, preloading and network prediction off, search suggestions off, HTTPS-First balanced, media router off, sign-in disallowed, translation off, native spell-check.
   - Feature overrides disabling Google-service and advertising features.
   - The component updater stays disabled until Phase 2's server exists.
   - A table-driven browser test asserts every default on a fresh profile, so an upstream rename fails loudly.
6. **Google services removed.** No API keys, no sync or sign-in, no Google endpoints. Safe Browsing stays off until its release gate is decided.

   **Baseline measured 2026-09-30** with `tools/egress_audit.py`: the first Ghost dev build, fresh profile, 10 minutes idle plus one local page. Each request was traced to its sender through its traffic annotation. **Measured again the same day** with the fixes below: no unexpected hosts.

   | Hosts | Sender | Fix |
   |---|---|---|
   | `update.googleapis.com` | Component updater | `--disable-component-update`, appended at startup by `ghost::BrowserMainExtraParts` (patch 0005). It comes back, pointed at our update server, in Phase 2. Until then, security data delivered as components, such as CRLSet and certificate transparency log lists, is not updated. |
   | `safebrowsing.googleapis.com` | Safe Browsing list updates | `safebrowsing.enabled` defaults to off; lists update only while some profile has it on. Release gate. |
   | `android.clients.google.com` (`/checkin`), `mtalk.google.com` | GCM check-in and push connection | GCM starts only with `GhostGoogleCloudMessaging` (patch 0006). Web Push does not work until we decide how to offer it. |
   | `accounts.google.com` (`/ListAccounts`) | Readers of the list of Google accounts signed in on the web: at startup, and whenever Google's sign-in cookies change. Browser sign-in itself is already off in builds without API keys. | The request is sent only with `GhostGoogleAccountsInCookieJar` (patch 0009). |
   | `www.google.com`, `www.gstatic.com`, `ogads-pa.clients6.google.com`, `play.google.com` (`/log`) | New Tab page with Google as the default search engine (logo, Google's bar and its logging, promos); AI Mode's eligibility check (`/async/folae`) | DuckDuckGo is the fallback search engine (patch 0007). Prepopulated engines' remote New Tab pages are dropped, so the page stays local (patch 0008). AI Mode is off (`AimEnabled`). |
   | `clients2.google.com` (`/time`) | Network time tracker | `NetworkTimeServiceQuerying` off. Certificate errors caused by a wrong clock are recognised against the build time instead, as on ChromeOS. |
   | `chromewebstore.googleapis.com`, `clients2.googleusercontent.com`, `clients2.google.com` (`/service/update2/crx`) | An extension pushed by another program through `HKLM\…\Google\Chrome\Extensions`, which Chromium reads whatever its brand, and downloaded before the user is asked | `extensions.block_external_extensions` defaults to on, which removes the registry source. Policy-installed and user-installed extensions are unaffected. |
   | `2001:4860:4860::8888` | Not traffic: the host resolver's IPv6 reachability probe, a UDP connect that sends nothing. The baseline attributed it to DNS-over-HTTPS. | The audit reports UDP sockets that carried no datagram as route probes, which never fail it. |

   **Found outside the idle audit**, while a page with a form was open during an audit run: Autofill sent the structure of every form to `content-autofill.googleapis.com` to have its fields classified. `AutofillServerCommunication` is off; Autofill fills from local heuristics. This is why step 7 added scenarios.

   Each fix has a test that fails without it: `ghost_unittests` for prefs, startup switches and the account list; `ghost_browsertests` for features, GCM, the search engine and the New Tab page.

   **Default search engine: DuckDuckGo.** It is in 123 of Chromium's 134 regional lists and comes from the full list elsewhere, and being the default requires no agreement. Whether to replace it under a commercial agreement is a release gate ([licensing.md](licensing.md#release-gates)). A user who picks Google gets Google's New Tab page, with its logo, bar and promos.

   **Also found while measuring:**
   - Dev builds activated `fieldtrial_testing_config.json` experiments until `disable_fieldtrial_testing_config` was set in `build/args/dev.gn`.
   - NetLogs contain the user's IP addresses. Raw audit logs stay local and are never committed; parser fixtures are scrubbed first ([test/egress/netlogs](../test/egress/netlogs/README.md)).
7. **Egress audit** (`test/egress/`, `tools/egress_audit.py`; see [testing.md](testing.md)).
   - [ ] Launch the packaged browser with a fresh `--user-data-dir` and `--log-net-log`. Until step 8 produces an installer, the audit runs the dev build.
   - [x] Idle for 10 minutes, then load a local page.
   - [x] Act out what users do on a site, on local pages: fill in and submit an address (`address`), and sign in (`login`). The omnibox, which DevTools can't drive, is tested in `ghost_browsertests`: typing sends nothing, and a search contacts only the search engine.
   - [x] Fail on any host outside the allowlist, which is empty apart from localhost in Phase 1.
   - [x] Parser tests use a scrubbed NetLog captured from our own build.

   **Measured 2026-09-30** on the dev build:

   | Hosts | Sender | Fix |
   |---|---|---|
   | `passwordsleakcheck-pa.googleapis.com` | Password leak check, after the `login` scenario's sign-in. It runs without Safe Browsing and without a Google account. | `kPasswordLeakDetectionEnabled` defaults to off. |
   | `wpad` | Proxy auto-detection, because Windows' "Automatically detect settings" is on. The parser had dropped single-label names. | Not a fix: Chromium follows the system's proxy settings. The audit reports these lookups and doesn't count them. |

   **Measured again** with the fix: 10 minutes idle, the local page, and both scenarios, with no unexpected hosts. No name was resolved on the internet during the whole run.
8. **Installer.** The `mini_installer` target, plus a smoke test: silent per-user install, registry and shortcut checks, launch, uninstall, cleanup check. Runs in Windows Sandbox or a VM.

   **Done 2026-09-30.** `tools/installer_smoke.py sandbox` passes on the dev build. It installs, launches and uninstalls in Windows Sandbox, and nothing is left behind ([testing.md](testing.md)).

   **Found on the way:**
   - The installer's strings said "Chromium" in shortcuts, Apps & features and installer messages. `branding/installer_strings.py` and patch 0010 now take the product name from `BRANDING`. The browser's own UI strings still say Chromium ([branding/README.md](../branding/README.md)).
   - Uninstall left the PDF ProgID behind. This is an upstream bug, [crbug.com/40384442](https://crbug.com/40384442), and patch 0011 fixes it.

   **Not covered yet:** system-level installs, and an official build's installer, which is signed from Phase 2.
9. **Versioning ADR.** [ADR 0007](adr/0007-version-numbers.md), accepted 2026-09-30.
   - Releases keep Chromium's MAJOR.MINOR.BUILD and put Ghost's respins into the fourth part (`PATCH × 100 + respin`). The installer and updater then see a growing number with no patch.
   - Websites and the Chrome Web Store keep seeing `CHROMIUM_VERSION`.
   - This replaces the first sketch here: a separate product version beside an upstream `chrome/VERSION`. The installer and updater read `chrome/VERSION` itself, so that sketch could not have shipped a Ghost-only fix.
   - Implementation lands with the release pipeline in Phase 2.
10. **Build host runbook.** Runbook and scripts for a self-hosted Windows builder. Registering it with CI requires maintainer approval.

    **Done 2026-09-30:** [the runbook](build/build-host.md), `tools/builder.py`, and `.github/workflows/build.yml`.
    - The workflow stays off until a maintainer registers the runner and sets `GHOST_BUILDER`.
    - `builder.py pr` ran end to end on the reference machine's checkout: the build, `ghost_unittests` 11/11 and `ghost_browsertests` 12/12. The sync and apply steps were skipped because the checkout was already current.
    - **2026-10-01:** `builder.py nightly` moved that checkout to the security release 152.0.7977.149, running every step. The sync and apply steps ran for the first time.
      - The build took 4 h 39 min for about 8,600 actions. The release rolls V8, so most of Blink recompiles.
      - `ghost_unittests` 11/11, `ghost_browsertests` 12/12, the installer smoke test and the egress audit (0 unexpected hosts) all passed.
      - Two browser tests passed only on a retry. The launcher had marked their first run `EXCESSIVE_OUTPUT`: over 500 KB of log, mostly Blink property trees, which a DCHECK build prints when `VLOG(1)` is on. They were the first tests in their batch.
      - From the upstream release (2026-09-29 18:30 UTC) to a tested build took 40 hours. That build was not signed or shipped; the 72-hour SLA applies from Phase 2.
    - It re-applies the series only when the series changes. Otherwise every run would rebuild the files the patches touch and their dependents: siso rebuilds by modification time, and `install_modes.h` alone reaches over a hundred files.

**Exit criteria** (status 2026-10-02: all met, Phase 1 is complete)
- [x] A clean checkout produces a branded installer through the documented steps.
  - On the reference machine, in an empty directory with the Defender exclusion in place.
  - The repository was cloned from GitHub at `41ec433`. The steps in [build/windows.md](build/windows.md) were then run as written: `check_env.py`, `bootstrap.py`, `patches.py apply`, `gn gen` with `dev.gn`, and `autoninja -j 10` of `chrome`, both test suites and `mini_installer`.
  - `ghost_unittests` passed 11/11 and `ghost_browsertests` 12/12, with no retries. The installer smoke test passed in Windows Sandbox.
  - **It found a bootstrap bug.** Fetching the tag into a new `--no-history` clone downloaded the 1.4 GB clone again, and on Windows git failed to rename it over the identical, read-only pack. Since `82b46ca` and `41ec433`, bootstrap checks the tag with `git ls-remote` and fetches only a commit it doesn't have.
  - Bootstrap took about 26 minutes in total, split across the runs the bug interrupted: about 19 for the clone and dependencies, about 7 for the hooks. The build took 14 h 07 min over two sessions: it stopped cleanly overnight and resumed where it left off.
  - The build is longer than the 11 h 17 min baseline, which built only `chrome`. Both test suites pull in Chromium's browser test support.
  - `out\vanilla` is 31 GB, and `mini_installer.exe` is 614 MB, which is a component build's size.
- [x] The browser browses and installs a Chrome Web Store extension.
  - The reference machine's dev build installed uBlock Origin Lite from its Web Store page, confirmed by hand.
  - The profile records the install as from the Web Store and enabled, and its service worker ran.
  - The browser's own requests went to `clients2.google.com` and `clients2.googleusercontent.com`: the download and the update check, which [privacy-model.md](privacy-model.md#data-the-browser-sends) allows.
  - The Web Store page itself loads Google Analytics, Tag Manager and Google's `/log` endpoint. Blocking in Phase 3 covers them.
- [x] The egress audit reports no unexpected hosts: 10 minutes idle, a local page, and the `address` and `login` scenarios (step 7). Rerun at 152.0.7977.149 (step 10).
- [x] The defaults test, `ghost_unittests` (11) and `ghost_browsertests` (12) pass, at 152.0.7977.149 too.
- [x] The patch series applies with `tools/patches.py`.
- [x] An informational rebase of the series onto the next milestone has been attempted, and its conflicts recorded.
  - `patches.py canary` onto 154.0.8037.93, the next Extended milestone, then on Stable: all 11 patches apply cleanly. Their files changed, but not their context.
  - The result was not built. Compile errors in `//ghost` code against 154's APIs would show only in a build.
  - Upstream's PDF ProgID bug (patch 0011) is still present in 154.

## Phase 2: release engineering

Runs under a **test identity** until the final name is chosen: test app IDs, a test server domain and test keys, on test machines only ([licensing.md](licensing.md#trademarks)). Moving to the final name is one step, through `branding/` and the server configuration, before the first public build.

Sub-projects, each with its own design and plan in `docs/superpowers/`:

1. **A. Release version.** [ADR 0007](adr/0007-version-numbers.md) in builds.

   **Done 2026-10-02.** `tools/release_version.py`, `//ghost/version`, patches 0012–0014; [design](superpowers/specs/2026-10-02-release-version-design.md), [spike results](superpowers/specs/2026-10-02-release-version-spike.md).
   - A respin (`152.0.7977.149-1`) installs in Windows Sandbox as `152.0.7977.14901`, while the web sees `152.0.7977.149`.
   - A respin rebuilds 380 actions, about 12 minutes on the reference machine.
   - `ghost_unittests` (17) and `ghost_browsertests` (15) pass with the two versions different. Each hook patch is mutation-checked.
2. **B. Branded `//chrome/updater`.** Our app IDs and server, installed with the browser. It also patches the updater client to register the release version (see the spike's audit).

   **Done 2026-10-03.** Patches 0015–0024, `tools/update_server.py`, `crx3.py`, `ecdsa_p256.py`, `offline_installer.py`, `update_smoke.py`; [design](superpowers/specs/2026-10-02-branded-updater-design.md), [spike results](superpowers/specs/2026-10-02-branded-updater-spike.md).
   - In Windows Sandbox, the offline installer installs the updater and respin `-1`; the updater takes respin `-2` from a CUP-signed response, as a CRX3 with Ghost's publisher proof; uninstalling the browser removes the updater too.
   - Every update request carries only the allow-listed keys, published in [privacy-model.md](privacy-model.md#the-update-request), and no event request is sent.
   - The browser now lives under a company directory: `%LOCALAPPDATA%\ProjectGhost\Browser`.
   - Five mutation checks fail the end-to-end test as required. One showed a gap in the test, fixed before the result was recorded.
   - Per-user only. For later sub-projects: C switches the component updater to Ghost's publisher proof; D replaces the test keys and signs and tags installers; system-level installs are their own sub-project before the public alpha.
3. **C. Update server.** Omaha 4, CUP-signed responses, no IP retention; components later.
4. **D. Signing.** Authenticode, the CUP key, the CRX3 key, and where the keys live.
5. **E. Release pipeline.** Official build from a tag, SBOM and provenance, staged rollout with a halt switch. It calls `release_version.py write` after `patches.py apply`.
6. **F. Security release runbook,** and one measured milestone move (to 154).

**Exit criteria**
- [ ] An update shipped end to end to test machines.
- [ ] One milestone move measured.
