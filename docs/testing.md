# Testing and CI

**The standard:** a test exists to fail when the behavior it describes breaks. A test that can't fail, or that only exercises a mock of the code under test, is deleted in review. When we claim a guarantee in [privacy-model.md](privacy-model.md), a test named in this document enforces it.

## Test layers

| Layer | Framework | Runs | Covers |
|---|---|---|---|
| Tooling | Python `unittest`, against real temporary git repositories | Every push (hosted CI) | `tools/`: environment rules, gclient generation, patch export and apply determinism, lint |
| Unit | gtest (`ghost_unittests`) | Every PR (build host) | Pure logic in `components/*/core`: policy resolution, parameter rules, identity rules, score formula, key derivation, list management, manifest parsing |
| Browser | `InProcessBrowserTest` (`ghost_browsertests`) with `EmbeddedTestServer` serving several HTTPS hostnames | Every PR (build host) | Integration with Chromium: interception, partitions, OTR lifecycle, defaults, UI entry points |
| Isolation contract | Browser tests, table-driven | Every PR | Every storage mechanism × every boundary; see [architecture.md](architecture.md#testing-isolation) |
| Fingerprinting | Browser tests evaluating script in frames, workers and media queries | Every PR | Consistency, determinism per site, unlinkability across sites, per-mode tiers |
| Egress audit | Harness driving the packaged browser; NetLog analysis | Nightly and release | No contact with hosts outside the allowlist |
| Installer smoke | `tools/installer_smoke.py` in Windows Sandbox | Nightly and release | Per-user install, registration, shortcuts, the product's icons, launch, uninstall, cleanup |
| Updater end to end | `tools/update_smoke.py` in Windows Sandbox, against `tools/update_server.py` | Release; run by hand until the builder runs it | Install through the updater, an applied update, the update request's allow-list, uninstall of the browser and the updater; for signed releases, every PE file's signature, the installer's tag and an update proved by the backup key |
| Upstream suites | Filtered `unit_tests`, `browser_tests`, `content_browsertests` | Nightly | Upstream behavior our patches touch |
| Fuzzing | libFuzzer | By hand now (blocking, since 3A); nightly, later continuous | List parsing and requests against the lists, parameter stripping, manifest parsing, every mojom handler we add |
| Performance | crossbench (Speedometer 3, JetStream, MotionMark) and a page-load corpus | Nightly | Regression against vanilla Chromium at the same tag; blocking overhead budget |
| Compatibility | Smoke corpus of popular sites | Nightly, never gating | Breakage from blocking and protections |

**Notes on specific layers**
- **Ghost session destruction** is tested twice:
  - behaviorally: state set in a session is absent from the next session;
  - on disk: the user-data directory is diffed before and after, and any new file outside an allowlist fails the test.
- **Real-site compatibility tests are flaky** by nature. They report trends and never block a merge.
- **The egress audit** is `tools/egress_audit.py`, with the allowlist at `test/egress/allowlist.json`.
  - `run --chrome <path>` launches a build on a fresh profile with a NetLog. It idles, loads a page served on loopback, acts out the scenarios, closes the browser, and fails on any host outside the allowlist.
  - **Scenarios** do what users do on a site, on pages in `test/egress/site/` served from loopback: `address` fills in and submits a shipping address, and `login` signs in with a username and a fresh random password. Clicks and key presses go through DevTools' input pipeline, so Autofill and the password manager see a user typing. `--scenarios` picks a subset.
  - The report names the phase (`startup`, `idle`, `local page`, a scenario, `shutdown`) in which each host was first seen.
  - `parse <netlog>` audits an existing log.
  - `scrub <netlog> <output>` reduces a log to what the audit reads, as a parser test fixture ([test/egress/netlogs](../test/egress/netlogs/README.md)).
  - Hosts are found by parameter name (`url`, `host`, `stream_key`, …) rather than by event type, so new NetLog events in later milestones are still covered.
  - **Reported but not counted:**
    - the system's DNS resolver;
    - UDP route probes, which send nothing;
    - lookups of `wpad`, made to auto-detect a proxy while Windows' "Automatically detect settings" is on. Chromium follows the system's proxy settings, and networks that configure proxies this way depend on it.
  - **The omnibox** is part of the browser's UI, which DevTools can't drive. `test/egress/omnibox_browsertest.cc` covers it in `ghost_browsertests`: it records every request the browser makes while the user types into the omnibox and searches, and fails on anything but the search itself.
  - **Blind spot:** NetLog sees only Chromium's network stack. Crashpad uploads crash reports from its own process, so crash upload stays disabled rather than relying on this audit.
  - **Not covered: payment cards.** Autofill handles cards only on HTTPS pages, and the loopback site is HTTP. Card requests to Google's payments servers carry an OAuth token for the signed-in Google account, and Ghost builds can't sign in.

- **The installer smoke test** is `tools/installer_smoke.py`. `sandbox --installer <mini_installer.exe>` runs it in a fresh Windows Sandbox with no network ([build/windows.md](build/windows.md#installer)). Expected names come from `branding/` and `CHROMIUM_VERSION`. It also checks the icons: the installer's, the installed `chrome.exe`'s and `setup.exe`'s first icon group (the one Explorer shows) must be `branding/theme/win/app.ico` byte for byte, and `chrome.exe` must carry the document icons; `tools/pe_resources.py` reads them, with the standard library only, so it runs in the Sandbox.
  - **After install:**
    - the browser and `setup.exe` are in place;
    - Apps & features shows the product name, publisher and version;
    - the browser is registered (StartMenuInternet and the HTML ProgID);
    - Start menu and Desktop shortcuts are named for the product and open it;
    - `chrome.exe`'s file properties name the product;
    - nothing is named Chromium, so Ghost can sit next to an installed Chromium.
  - **Launch:** the installed browser starts and reports the pinned version over DevTools.
  - **After uninstall:** none of it is left.
  - `run` is the test itself. It installs into the current user's profile, so outside Windows Sandbox it runs only with `--disposable`.

- **The updater's end-to-end test** is `tools/update_smoke.py`. `sandbox` takes an offline installer for one release and a CRX3 of the next ([build/windows.md](build/windows.md#offline-installer)), and runs these steps in a fresh Windows Sandbox, with `tools/update_server.py` serving on loopback:
  - **install:** the offline installer installs the updater and its scheduled task, then the browser, which passes the installer smoke test's checks and is registered with the updater at its release version;
  - **update:** the updater, woken, takes the CRX3 from the server's CUP-signed response, and the next release is installed beside the first;
  - **launch:** the updated browser starts and reports the Chromium release over DevTools;
  - **privacy:** every request the server logged is an update check (an event request reaches the server scrubbed, as an app without `updatecheck`) and carries only allow-listed keys, and the updater's log names no host but the server;
  - **uninstall:** setup removes the browser, then the updater, woken, finds no app and removes itself, its scheduled task and its registry key.
  - Mutation checks showed each check fails when the hook it covers is undone; the results are in the [spike notes](superpowers/specs/2026-10-02-branded-updater-spike.md#mutation-checks).
  - **Signed releases** ([signing](signing/README.md)) add these steps and options:
    - `--codesign-cert` makes the sandbox **trust the signing certificate** (`Root` and `TrustedPublisher`) before the install. Then **signatures**, after the install, requires every PE file under the browser's and the updater's directories to carry a valid signature by that certificate; **signatures after the update** checks the browser's directory again.
    - `--tagged` runs the offline installer with only `--silent`, so the install succeeds only if its tag names the app.
    - `--recovery-crx` adds the **recovery update (backup publisher key)** after the update: a third release whose publisher proof is made by the backup key, as when the primary is lost.
    - Four mutation checks showed these fail as required: a CRX3 proved by a third key, an unsigned `chrome.dll`, an untagged installer, and an updater pinning only the primary publisher key. The results are in the [progress notes](superpowers/specs/2026-10-04-signing-spike.md#mutation-checks).
  - **Rollout** ([release.md](build/release.md#rolling-out)): `--rollout-server` runs the update server repository's own service in the Sandbox and offers the update as its candidate. **halted (fraction 0):** the updater checks and stays on the installed release; **rolled out (fraction 1):** it takes the update. Four mutation checks cover the release pipeline: a local change in the Chromium checkout, a server ignoring the fraction, a wrong hash in the provenance, and `--public` with the test identity ([progress notes](superpowers/specs/2026-10-05-release-pipeline-spike.md#mutation-checks)). The Sandbox maps only the base Python installation, so the service's `cryptography` and its `cffi` must be installed there, not in the user's site-packages (`python -s -m pip install --no-user …`); `update_smoke.py` checks before starting a Sandbox.

- **Blocking** (Phase 3A; [progress notes](superpowers/specs/2026-10-10-blocking-engine-spike.md)).
  - **Unit** (`ghost_unittests`): the engine through its bridge with lists written in the test; `BlockingEngine`'s states, a check made while it loads, and input that isn't UTF-8; registrable domains; the request-type mapping; reading the lists.
  - **Browser** (`ghost_browsertests`, `RequestFilterBrowserTest`), with test lists and hosts mapped to the embedded test server: a third-party tracker's script blocked and never reaching the server; a frame failing with `ERR_BLOCKED_BY_CLIENT`; the page's own site not checked, with a rule that would block it; a redirect to a tracker blocked; a top-level navigation loading; Incognito; the shipped EasyList and EasyPrivacy blocking known hosts.
  - **Performance:** `ghost_blocking_perftests` checks 2,194 requests recorded from 20 news front pages (`components/blocking/test/request_corpus.tsv`, query values replaced) against the shipped lists, and reports compile time, memory and p50/p99. It fails over ADR 0006's 50 µs at p99 only in an official build: a development build compiles Rust with debug assertions, about 15 times slower. Its own binary, so that it builds in minutes in `out/release`.
  - **Fuzzing,** by hand for now: `ghost_blocking_list_fuzzer` (list text) and `ghost_blocking_request_fuzzer` (requests against the shipped lists, in the corpus' format). A fuzzing output directory builds only them (about 30 minutes):

    ```
    gn gen out\fuzz --args="import(\"//ghost/build/args/fuzz.gn\")"
    autoninja -C out\fuzz ghost_blocking_list_fuzzer ghost_blocking_request_fuzzer
    out\fuzz\ghost_blocking_list_fuzzer.exe -max_total_time=1800 -max_len=4096 -dict=ghost\components\blocking\fuzz\filter_list.dict <corpus> <seeds>
    out\fuzz\ghost_blocking_request_fuzzer.exe -max_total_time=1800 <corpus> <seeds>
    autoninja -C out\fuzz ghost_query_filter_fuzzer
    out\fuzz\ghost_query_filter_fuzzer.exe -max_total_time=1800 -max_len=8192 <corpus> <seeds>
    ```

    Seeds: the shipped lists cut into pieces under 4 KB, and one file per line of the request corpus. `//ghost/build/config:rust_fuzz_coverage` instruments `//ghost`'s Rust code, which Chromium's fuzzing build leaves blind.
  - **Installer:** `installer_smoke` checks that both lists are installed in `<version>\blocking\`.
  - Mutation checks showed each of these fails when the behavior it covers is undone ([progress notes](superpowers/specs/2026-10-10-blocking-engine-spike.md#mutation-checks)).

- **Tracking parameters** (Phase 3B; [progress notes](superpowers/specs/2026-10-10-query-filter-spike.md)).
  - **Unit** (`ghost_unittests`): the list's parser and the shipped list (no line may be skipped); `Strip()`, byte for byte, with encoded names, repeats, site-tied entries and both scopes; the decisions (from the user, from another site, opaque, the same site; GET only); the pref's default.
  - **Browser** (`ghost_browsertests`, `QueryFilterBrowserTest`), on the embedded server, which records every request: a link from another site and the address bar arrive clean and the original never reaches the server; a link within a site keeps its parameters; a cross-site redirect is cleaned, a same-site one isn't; history holds only the clean URL; back, forward, reload and loading the entry's original URL stay clean; `utm_*` in Normal, Incognito and with the pref; iframes and form posts untouched.
  - **Fuzzing:** `ghost_query_filter_fuzzer` (a URL, then a list) checks that stripping keeps scheme, host and port and that a second pass changes nothing. Seeds: URLs from the blocking corpus with listed parameters appended.
  - Mutation checks: [progress notes](superpowers/specs/2026-10-10-query-filter-spike.md#mutation-checks).

- **WebSocket and WebTransport** (Phase 3D-2; [progress notes](superpowers/specs/2026-10-10-websocket-webtransport-spike.md)). `ConnectionFilterBrowserTest` runs `EmbeddedTestServer`'s WebSocket handlers, whose request monitor shows whether a handshake arrived, and `content::WebTransportSimpleTestServer` with QUIC forced for the tracker's name too, so that an unblocked tracker connects and the refusal test can fail.
- **Protections panel** (Phase 3D-3; [progress notes](superpowers/specs/2026-10-10-protections-panel-spike.md)). Unit: `PageProtectionsTest` (the count per page, frames of another page ignored) and `PanelStateTest` (the site, where protections apply, choosing the default clears the choice, the reload). Browser: `BlockedCountBrowserTest` counts images, a subframe, a dedicated worker, a WebSocket, a navigation's reset and a background tab; `ProtectionsPanelBrowserTest` clicks the toolbar button by its element identifier, reads the panel's page with `EvalJs` in the bubble's `WebContents` and clicks a level there. Real WebUI pages crash under `ChromeRenderViewHostTestHarness`, so unit tests use a chrome: URL without a page.
- **Protection levels** (Phase 3D-1; [progress notes](superpowers/specs/2026-10-10-protection-levels-spike.md)). Unit: each level's policy, the pref's keying by site, the modes' defaults, Incognito's inheritance. Browser (`ProtectionLevelsBrowserTest`): Off, Standard and Strict pages with a test list whose `/own.js` rule only a same-site check applies; the `Referer` the server receives from Strict and Standard pages, for subresources and navigations; Incognito strict by default; a level applying at the next load; parameters kept for an Off site, campaign parameters stripped for a Strict one.
- **Network defaults** (Phase 3C; [progress notes](superpowers/specs/2026-10-10-network-defaults-spike.md)). `test/network_defaults_browsertest.cc` proves each promise of [privacy-model.md](privacy-model.md#cookies-and-storage)'s "Cookies and storage" and "Connections and DNS" by its behavior where it can be observed: what the embedded server receives (`Sec-GPC`, `Referer`, cookies), what a page and a worker see, the warning page HTTPS-First shows, and the connections a second server accepts; and by Chromium's own decision where it can't (WebRTC's renderer preference, Privacy Sandbox settings, DNS-over-HTTPS, DIPS). Five mutation checks fail it as required. `test/etw_logging_browsertest.cc` checks that the browser registers no ETW log provider (patch 0033).

## Running the tooling tests

```
python -m unittest discover -s tools/tests -t tools
python tools/lint.py
```

They need the packages in `tools/requirements.txt` (`python -m pip install -r tools/requirements.txt`), Pillow among them: `test_brand_icons.py` regenerates every icon from `branding/logo/` and compares it with the committed `branding/theme/`, by decoded pixels, so PNG compression differences between platforms don't matter. Without Pillow the test fails rather than skips.

The tooling tests run git with system and global config disabled. The CI runners' `core.autocrlf` and the developer's `diff.*` and `format.*` settings therefore can't change results.

## CI

**Hosted runners (GitHub Actions), from Phase 0**
- `tooling.yml` runs the tooling tests and lint on Windows and Ubuntu for every push and pull request.
- Actions are pinned by commit SHA, and the workflow token is read-only.

**Self-hosted Windows build host, from Phase 1.** `tools/builder.py` runs the builds, and `.github/workflows/build.yml` schedules them. Setup and operation are in the [runbook](build/build-host.md).
- **Persistent checkout and build cache.** A clean Chromium build takes hours, so pull-request builds are incremental. Chromium is re-synced only when the pin moves, and the series is re-applied only when it changes.
- **Pull requests and pushes to `main`:** apply the series, build `chrome`, `ghost_unittests`, `ghost_browsertests` and `ghost_blocking_perftests`, and run them.
- **Nightly, now:** the same, plus the installer, its smoke test, and the egress audit.
- **Nightly, planned:**
  - an official-configuration build;
  - the isolation suite;
  - upstream suites, performance and compatibility runs;
  - the **canary rebase** of the series onto the current Beta tag ([ADR 0003](adr/0003-upstream-extended-stable.md)).
- **Fork pull requests** never run automatically on the builder. A maintainer adds the `safe to build` label after reviewing the change. Commits pushed afterwards need the label again.

**Release pipeline, from Phase 2**
- A separate builder that runs only tagged releases, from a clean checkout.
- Signing goes through a hardware-backed service. Stable releases need two maintainers' approval.
- Each release publishes an SBOM (SPDX) and provenance (SLSA) next to its artifacts.
- Rollout is staged by percentage, with a halt switch.

**Later.** When build minutes dominate cost, evaluate a remote-execution backend compatible with Chromium's build tool (Siso speaks the Remote Execution API).
