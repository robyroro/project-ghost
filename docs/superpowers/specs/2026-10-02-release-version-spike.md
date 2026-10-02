# Release version: spike results

- Date: 2026-10-02
- `CHROMIUM_VERSION`: 152.0.7977.149
- Respin used: tag `152.0.7977.149-1`, release version `152.0.7977.14901`
- Machine and build: the reference machine (Ryzen 5 3600, 32 GB), `out/vanilla` (`dev.gn`: component build, `symbol_level=1`), `autoninja -j 10`
- Design: [2026-10-02-release-version-design.md](2026-10-02-release-version-design.md)

## Result

ADR 0007 works end to end in a Windows Sandbox install of the spike's installer:
- the install directory is `…\Project Ghost\Application\152.0.7977.14901\`;
- Apps & features shows `DisplayVersion` `152.0.7977.14901`;
- the running browser reports `Chrome/152.0.7977.149` over DevTools.

Every test passed with the two versions different: `ghost_unittests` 17/17 and `ghost_browsertests` 15/15, with no retries.

## Build times

| Change | Targets | Time | Actions |
|---|---|---|---|
| Patch 0012 added, development version (the generated header doesn't change, but what includes it recompiles once) | `ghost_unittests` | 7 min 25 s | 325 |
| Patch 0013, upgrade detector | `chrome`, `ghost_unittests` | 1 min 37 s | — |
| Patch 0014, `version_ui` | `chrome`, `ghost_browsertests` | 1 min 39 s | — |
| **A respin:** `chrome/VERSION` from `PATCH=149` to `PATCH=14901` | `chrome`, both suites, `mini_installer` | **11 min 37 s** | 380 |
| Back to the development version | same | 7 min 33 s | 380 |

**What a respin costs.** With the hook, a respin no longer changes `version_info`. It changes only what reads `chrome/VERSION` directly: the version resources of every binary, `chrome/common`'s version header and the installer. That is 380 actions, about 12 minutes on this machine. A release builder runs it once per release, on top of an up-to-date build.

## Audit of `version_info` callers

46 non-test `.cc` files under `chrome/browser`, `chrome/app`, `chrome/common`, `chrome/installer/util` and `components/component_updater` call `version_info::GetVersion()` or `GetVersionNumber()` (Android, ChromeOS, macOS and Linux files excluded). The 20 that mention installs, setup, upgrades or relaunches were read call by call.

**Changed in this sub-project:**

| File | Call | Decision |
|---|---|---|
| `chrome/browser/upgrade_detector/installed_version_poller.cc` | compares the installed version with the running one; simulates an upgrade from it | release version (patch 0013) |
| `chrome/browser/upgrade_detector/upgrade_detector_impl.cc` | compares the critical version with the running one | release version (patch 0013) |
| `chrome/browser/ui/webui/version/version_ui.cc` | About and `chrome://version` | display version (patch 0014) |

**Requirements for later sub-projects:**

| File | Call | Decision |
|---|---|---|
| `chrome/browser/updater/browser_updater_client_win.cc` | registers the browser with the updater, giving its version | Must be the release version, or the updater would offer the installed release again. Not compiled today (`enable_updater = false`). **Sub-project B** patches it when it turns the updater on. |
| `chrome/browser/component_updater/recovery_improved_component_installer_win.cc` | gives the browser version to Google's recovery component | Must be the release version if a recovery component ever runs. The component updater is off, and Ghost doesn't serve this component. **Sub-projects B and C** decide. |

**Correct as they are, keeping the Chromium release:**

| File | Call | Why it's right |
|---|---|---|
| `chrome/browser/updater/updater_no_updater.cc` | `CurrentlyInstalledVersion()` | No caller outside tests. |
| `chrome/browser/downgrade/downgrade_manager.cc` | compares the profile's "Last Version" file with the running version | The file is written from `version_info`, so both sides use the same version. Respins of one Chromium release share a profile format. |
| `chrome/browser/chrome_browser_main_win.cc` | taskbar-pin migration version in Local State | Written and read through `version_info`. |
| `chrome/browser/ui/startup/default_browser_prompt/default_browser_prompt.cc` | "don't ask again" version in prefs | Written and read through `version_info`. |
| `chrome/browser/extensions/chrome_extensions_browser_client.cc` | `kLastChromeVersion`; extensions' minimum browser version | Profile data written and read through `version_info`; minimums are stated against Chromium releases (ADR 0007). |
| `chrome/common/extensions/manifest_handlers/minimum_chrome_version_checker.cc` | `minimum_chrome_version` | Same as above. |
| `chrome/browser/extensions/preinstalled_extensions.cc` | whether the profile was created by this version | Compared with a version written through `version_info`. |
| `chrome/browser/metrics/variations/chrome_variations_service_client.cc` | version for the variations service | Reported to a server: the Chromium release, as for the web. Variations are off in Ghost. |
| `chrome/browser/privacy_sandbox/notice/notice_storage.cc` | version stored with a notice event | Profile data, written and read through `version_info`. |
| `chrome/browser/search_engines/ui_thread_search_terms_data.cc` | version in search terms data sent to search engines | Sent out, so the Chromium release. |
| `chrome/browser/signin/signin_hats_util.cc` | version in survey data | Sent to Google, so the Chromium release. Sign-in is off in Ghost. |
| `chrome/browser/ui/autofill/risk_util.cc` | version in the payments risk fingerprint | Sent to Google, so the Chromium release. |
| `chrome/app/chrome_main_delegate.cc` | `--version` and `--product-version` output | The Chromium release, printed for scripts. About shows the release. |
| `chrome/browser/diagnostics/recon_diagnostics.cc` | checks that a version exists | No comparison. |
| `chrome/browser/ui/webui/flags/flags_ui.cc`, `inspect/inspect_ui.cc`, `policy/policy_ui.cc` | version on `chrome://flags`, `chrome://inspect`, `chrome://policy` | Diagnostic pages. They show the Chromium release; About and `chrome://version` show the release. |
| `chrome/browser/upgrade_detector/upgrade_detector.cc` | asks the updater when this version was last served | Needs the updater. **Sub-project B** checks it with the updater on. |

The other 26 files report the version to servers or show it in diagnostics. None of them compare it with an installed version or build install paths.

## Mutation checks

Each hook patch was reverted in the checkout only, with the respin still written. Its tests had to fail:

| Patch reverted | Rebuild | Failed as required |
|---|---|---|
| 0012 `version_info` | 6 min 40 s | `VersionTest.VersionInfoIsTheChromiumRelease`: `version_info` was `152.0.7977.14901`. `WebVersionBrowserTest.PagesSeeTheChromiumRelease`: the page saw the release version, and not the Chromium release. |
| 0013 upgrade detector | 6 min 25 s | `InstalledVersionPollerTest.TheInstalledReleaseIsNotAnUpdate`: an update was detected (`kNormalUpdate`). |
| 0014 `version_ui` | 1 min 23 s | `VersionDisplayBrowserTest.AboutShowsTheDisplayVersion` and `ChromeVersionShowsTheDisplayVersion`: neither page showed `152.0.7977.149-1`. |

## Not covered

- **The "relaunch to update" prompt in a running install.** Upgrade detection is checked through `InstalledVersionPoller` in a unit test, not in an installed browser. Sub-project B, with the updater on, checks the whole path.
- **System-level installs**, as in Phase 1.
