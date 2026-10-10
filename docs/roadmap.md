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

**How we meet it (2026-10-10):** `.github/workflows/upstream.yml` checks Extended Stable every three hours and opens an issue, which notifies the maintainer; on call is that one person. [The runbook](build/security-release.md) goes from the issue to a verified release; its first drill took 22 h from start to a verified draft on the reference machine. Not automated yet: the third row, fixes on Stable not yet on Extended, which needs Stable's security notes read against our milestone.

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
      - Two browser tests passed only on a retry. The launcher had marked their first run `EXCESSIVE_OUTPUT`: over 500 KB of log, mostly Blink property trees, which a DCHECK build prints when `VLOG(1)` is on. They were the first tests in their batch. *Found in Phase 3C: the cause was a Windows telemetry trace session enabling Chromium's ETW log provider at the verbose level, which lowers every process's log level; patch 0033 removes the provider, and no test has needed a retry since.*
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

   **Code done 2026-10-03; deployment deferred.** [design](superpowers/specs/2026-10-03-update-server-design.md), [plan](superpowers/plans/2026-10-03-update-server.md), [progress notes](superpowers/specs/2026-10-03-update-server-spike.md), repository [project-ghost-update-server](https://github.com/robyroro/project-ghost-update-server).
   - The service, `ghost-update-admin`, the release CLI and the deployment files (Caddy, nftables, systemd, `provision.sh`) are written test-first; CI passes on Ubuntu with Debian 12's `cryptography` and on Windows. Four mutation checks fail as required.
   - `//ghost`: the update URL is a build argument (`ghost_update_url`); `tools/update_smoke.py` has a remote mode and an online-installer mode. The local end-to-end test still passes.
   - **Not yet verified:** nothing has run on a Linux server. `provision.sh`, the Caddy and nftables configuration, the systemd hardening and the "no addresses kept" check run for the first time on a VPS, with plan Tasks 13, 14 (Step 4) and 16. Deferred by decision on 2026-10-03; a temporary VPS is enough, and the domain can come later.
4. **D. Signing.** Authenticode, the CUP key, the CRX3 key, and where the keys live.

   **Done 2026-10-05.** `tools/signing.py`, `tpm.py`, `authenticode.py`, `mini_installer.py`, `ceremony.py`, `sign_release.py`; patch 0018 takes two publisher keys; [design](superpowers/specs/2026-10-04-signing-design.md), [progress notes](superpowers/specs/2026-10-04-signing-spike.md), [signing](signing/README.md).
   - All free, under the test identity: the publisher primary and the Authenticode key in the reference machine's TPM, the publisher backup off this PC (the user's cloud storage, encrypted with a password stretched 600,000 times), the CUP key on the server with versions.
   - In Windows Sandbox, the signed and tagged offline installer installs with only `--silent`; every installed PE file has a valid signature; the updater takes respin `-2` signed in the TPM, then respin `-3` signed with the backup.
   - Four mutation checks fail the end-to-end test as required.
   - Bought hardware (a YubiKey pair) and a bought or donated code-signing certificate come at the end, as a change of backend or certificate.
5. **E. Release pipeline.** Official build from a tag, SBOM and provenance, staged rollout with a halt switch. It calls `release_version.py write` after `patches.py apply`.
   **Done 2026-10-08.** `tools/release.py`, `release_state.py`, `sbom.py`, `provenance.py`, `candidate_server.py`; `build/args/release.gn`, `build/compute_build_timestamp.py`; patches 0025 (late component checks) and 0026 (`//base` takes the Chromium release); in the update server, the candidate and `stage`, `set-fraction`, `halt`, `promote`, `drop`; [design](superpowers/specs/2026-10-05-release-pipeline-design.md), [progress notes](superpowers/specs/2026-10-05-release-pipeline-spike.md), [release.md](build/release.md).
   - Official builds (ThinLTO, PGO) on the reference machine: 13 h 16 min for a Chromium version, 23 min of build per respin (41 min tag to draft).
   - `152.0.7977.149-3` and `-4` released as GitHub drafts, each with an SBOM (SPDX), SLSA Build Level 1 provenance and checksums; the egress audit on the official build finds no unexpected host. `-1` failed that audit (a late component check, patch 0025); `-2` was stopped and never finished.
   - In Windows Sandbox, the update server's own service holds `-4` as a candidate: halted, the updater stays on `-3`; at fraction 1, it takes `-4`.
   - Four mutation checks fail as required.
6. **The product name: Shade.** The Windows identity, the installers, the updater, the icons and the release's names, before F so that F's first official release carries them.

   **Done 2026-10-09.** `tools/brand_icons.py`, `pe_resources.py`; `branding/` renamed, `branding/logo/` and `branding/theme/`; patch 0027 (the icons from `//ghost/branding/theme`); the tools read the names from `branding/`; [design](superpowers/specs/2026-10-09-shade-rebrand-design.md), [progress notes](superpowers/specs/2026-10-09-shade-rebrand-spike.md).
   - Installs as `%LOCALAPPDATA%\Shade\Browser`, named "Shade" in Start, on the taskbar and in Apps & features; the installer smoke test proves the installed `chrome.exe`, `setup.exe` and the installer carry the committed icons. The updater's end-to-end test passes with the new names, and the egress audit finds no unexpected host.
   - Three mutation checks fail as required.
   - Still upstream's: "Chromium" in the browser's menus and dialogs (a later sub-project). Open release gates: the trademark search and buying browseshade.com ([licensing](licensing.md#trademarks)).
7. **F. Security release runbook,** and one measured milestone move (to 154).

   **Security releases done 2026-10-10.** `tools/upstream.py` (`check`, `report`, `bump`), `.github/workflows/upstream.yml`, [the runbook](build/security-release.md); [design](superpowers/specs/2026-10-09-security-release-design.md), [the first drill](security/drills/2026-10-09-152.0.7977.158.md).
   - Every three hours a workflow compares Extended Stable with the pin and opens an issue for a security release or a new milestone. `bump` moves the pin, the Chromium branch and the series in one command, after checking the tag against chromiumdash and the series with the canary.
   - Drill on 152.0.7977.158: issue to verified draft in 22 h 13 min (two builds 15 h 34 min, the PIN's overnight wait 5 h 34 min), inside 72 h from our start. It found three gaps, fixed before tagging, and the runbook's wrong `verify` command.
   - Three mutation checks fail as required.
   - **Still open:** the milestone move to 154, when Extended Stable reaches it (expected around 2026-10-20).

**Exit criteria**
- [ ] An update shipped end to end to test machines.
- [ ] One milestone move measured.

## Phase 3: network protections

Sub-projects, in order (decided 2026-10-10), each with its own design and plan in `docs/superpowers/`:

1. **3A. The blocking engine.** adblock-rust with EasyList and EasyPrivacy on every request a page makes ([ADR 0006](adr/0006-blocking-engine-adblock-rust.md)).

   **Done 2026-10-10.** `//ghost/components/blocking`, `//ghost/browser/blocking`, `//ghost/third_party/rust` (16 crates), `tools/rust_vendor.py`, `filter_lists.py`; patches 0028 (Chromium's crates), 0029 (the request filter's hook), 0030 (the lists in the installer), 0031 (`about:credits`); [design](superpowers/specs/2026-10-10-blocking-engine-design.md), [progress notes](superpowers/specs/2026-10-10-blocking-engine-spike.md).
   - Every profile blocks at the Standard level: third-party ads and trackers, with every redirect checked; top-level navigations and the page's own site pass ([privacy-model.md](privacy-model.md#blocking)).
   - In the official build a check costs 6 µs at p50 and 36–39 µs at p99, within ADR 0006's 50 µs; the lists compile in about 80 ms at startup into about 6 MiB.
   - Two fuzzers ran 30 minutes each without a crash; four mutation checks fail as required (one after a fix to its test); the egress audit finds no unexpected host.
   - Found on the way: bytes that aren't UTF-8 reaching the Rust bridge aborted the browser, and `about:credits` named none of the crates or lists. Both fixed.
   - Not yet: list updates between releases (3E). WebSocket and WebTransport followed in 3D-2.
2. **3B. Tracking-parameter stripping.**

   **Done 2026-10-10.** `//ghost/components/query_filter`, `//ghost/browser/query_filter`, `//ghost/components/site` (3A's registrable domains, shared), `tools/embed_text.py`; patch 0032; [design](superpowers/specs/2026-10-10-query-filter-design.md), [progress notes](superpowers/specs/2026-10-10-query-filter-spike.md).
   - Click identifiers (73 entries with `utm_*`) are removed from top-level navigations that come from another site or from the user, and from cross-site redirects, before the request is sent; the page commits at the clean URL. `utm_*` in Incognito, and in Normal with a pref ([privacy-model.md](privacy-model.md#tracking-parameters)).
   - The fuzzer ran 30 minutes without a crash; four mutation checks fail as required; the egress audit finds no unexpected host.
   - Kept on this machine: the history entry's original request URL (loading it again is cleaned again).
3. **3C. Network defaults:** third-party cookies, HTTPS-First, DNS-over-HTTPS, WebRTC, GPC, preconnect and prefetch off.

   **Done 2026-10-10.** `test/network_defaults_browsertest.cc`, `browser/startup/incognito_defaults`; [design](superpowers/specs/2026-10-10-network-defaults-design.md), [progress notes](superpowers/specs/2026-10-10-network-defaults-spike.md).
   - New: Global Privacy Control on; WebRTC on the default public interface; strict HTTPS-First in Incognito (upstream's is only balanced).
   - Found: a Windows telemetry session (DiagTrack) enabled Chromium's ETW log provider at the verbose level, receiving the browser's log messages. Patch 0033: Shade registers no provider. It also ends the browser tests' `EXCESSIVE_OUTPUT` retries.
   - Proven by behavior: every promise of the privacy model's "Cookies and storage" and "Connections and DNS", eleven tests; five mutation checks fail as required; the egress audit finds no unexpected host.
4. **3D. Per-site policy and the protections panel,** with the Strict and Off levels, WebSocket and WebTransport. Four sub-projects (decided 2026-10-10): 3D-1 the per-site policy and the levels; 3D-2 WebSocket and WebTransport; 3D-3 the protections panel; 3D-4 settings (the default level, GPC, campaign parameters).

   **3D-1 done 2026-10-10.** `//ghost/components/privacy_policy`, `//ghost/browser/privacy_policy`; patch 0029 amended; [design](superpowers/specs/2026-10-10-protection-levels-design.md), [progress notes](superpowers/specs/2026-10-10-protection-levels-spike.md).
   - Off, Standard and Strict per site; Incognito Strict by default. Blocking, tracking parameters and the referrer follow the page's level; GPC, cookies and HTTPS-First don't change with it.
   - Four mutation checks fail as required; 58 browser tests pass with no retry.
   - Limitation: a Strict page's cross-site navigation sends no `Referer`, but the destination's `document.referrer` still shows the origin.

   **3D-2 done 2026-10-10.** `//ghost/browser/blocking/connection_filter`; patch 0034; [design](superpowers/specs/2026-10-10-websocket-webtransport-design.md), [progress notes](superpowers/specs/2026-10-10-websocket-webtransport-spike.md).
   - A page's WebSocket and WebTransport connections are judged by the engine at the page's level; a blocked WebSocket's handshake never leaves the browser.
   - Three mutation checks fail as required; 65 browser tests pass with no retry.
   - Limitation: shared and service workers' WebSockets are judged at Standard (no profile in the hook).

   **3D-3 done 2026-10-11.** `//ghost/browser/protections`, `//ghost/browser/ui/protections`, `//ghost/browser/resources/protections`; patches 0035 (toolbar), 0036 (the page's registration), 0037 (the request filter's frame); [design](superpowers/specs/2026-10-10-protections-panel-design.md), [progress notes](superpowers/specs/2026-10-10-protections-panel-spike.md).
   - Shade's first UI: a toolbar shield with the count of requests blocked on the page opens a WebUI panel that sets the site's level; the look approved by the user.
   - Four mutation checks fail as required; 91 unit and 77 browser tests pass with no retry.
   - Limitations: shared and service workers' blocks aren't counted; the task manager names the panel "Privacy and security" until Shade's strings get a grd; strings are English only.
5. **3E. List updates as signed components,** which needs the update server (sub-project C) deployed.

**Exit criteria** ([phases](#phases-to-public-alpha))
- [ ] Blocking, parameter and egress tests green.
- [x] Blocking overhead within budget (3A).
