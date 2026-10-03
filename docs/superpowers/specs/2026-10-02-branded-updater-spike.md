# Branded updater: spike results

- Date: 2026-10-02 to 2026-10-03
- `CHROMIUM_VERSION`: 152.0.7977.149
- Releases used: respin `-1` (`152.0.7977.14901`) in the offline installer, respin `-2` (`152.0.7977.14902`) as the update
- Machine and build: the reference machine (Ryzen 5 3600, 32 GB); the browser installer from `out/vanilla` (`dev.gn`), the updater and its metainstaller from a static `out/updater` (`dev.gn` with `is_component_build=false`); `autoninja -j 10`
- Design: [2026-10-02-branded-updater-design.md](2026-10-02-branded-updater-design.md)

## Result

**The end-to-end test passed on 2026-10-02 in Windows Sandbox** (`tools/update_smoke.py`), in about 4 minutes:
1. **install:** the offline installer installed the updater, its scheduled task and the browser at `%LOCALAPPDATA%\ProjectGhost\Browser\Application`. Setup registered `Software\ProjectGhost\Update\Clients\{appid}` with `pv` = `152.0.7977.14901`. Everything `installer_smoke.py` checks passed.
2. **update:** the woken updater took respin `-2` from the test server's CUP-signed response; `pv` became `152.0.7977.14902`, installed beside the first.
3. **launch:** the updated browser reported `152.0.7977.149` over DevTools.
4. **privacy:** the server logged 3 requests: the browser's update check, and two checks the updater made for itself. Every one carried only allow-listed keys, and none was an event request. The updater's log names no host but `127.0.0.1`.
5. **uninstall:** setup removed the browser; the woken updater found no app and removed itself, its scheduled task and `Software\ProjectGhost\Update`.

`ghost_unittests` passes 25/25, with the new tests for the CRX format, CUP with Ghost's key and the scrubber. The tooling tests, with those for `update_server.py`, `crx3.py`, `ecdsa_p256.py` and `offline_installer.py`, pass.

## Build times

| Build | Targets | Time | Actions |
|---|---|---|---|
| **First static updater build** (`out/updater`) | `chrome/updater/win/installer:installer`, `chrome/updater/win:signing`, `chrome/updater/win:updater` | **44 min** | — |
| A change to `update_client` (a mutation check), static | the same | 38 s | — |
| Respin `-1`, browser installer and component-build updater targets | `mini_installer`, the three updater targets in `out/vanilla` | 7 min 2 s | 469 |
| Respin `-2` | `mini_installer` | 6 min 40 s | 377 |
| Offline installer (`offline_installer.py`, upstream's `sign.py` without signing) | — | about 35 s | — |

The rebuild after turning `USE_GOOGLE_UPDATE_INTEGRATION` on was not timed separately.

## End-to-end failures and their fixes

Each was found by a run of the end-to-end test, fixed where the plan says (a test detail in `update_smoke.py`, an identity value in `branding/`, a hook in its patch), and the test re-run.

| Failure | Cause | Fix |
|---|---|---|
| `UpdaterSetup.exe` didn't start in the Sandbox (`STATUS_DLL_NOT_FOUND`) | A component build's updater depends on about two dozen of Chromium's DLLs, and runs alone on a machine | The metainstaller comes from a static `out/updater` (commit 94ff934). The browser installer is self-contained and still comes from the component build. |
| The updater didn't remove itself | Its `uninstall.cmd` checks the install path with an unquoted `FindStr`, which read `Project Ghost` as a search string and a file name | The company directory is `ProjectGhost`, as `Google` and `BraveSoftware` have no space; the name people see stays Project Ghost (commit d443c07) |
| `--uninstall-if-unused` still counted the browser after its uninstall | Run alone, it uses the updater's stored state | The test runs `--wake`, which notices the app is gone, then starts the uninstall itself (commit d443c07) |
| `Software\ProjectGhost\Update` was left after the updater's uninstall | Upstream deletes `ClientState` but leaves the updater's own `Clients` entries and an `UninstallCmdLine` naming the deleted `updater.exe` | Patch 0024: once no app remains, the whole key goes |
| The product key wasn't empty after uninstall | A normal uninstall keeps the profile, and upstream clears `InstallerPinned`, `UsageStatsInSample`, `PreferenceMACs` and `StabilityMetrics` only with it. Phase 1 never saw them: the product key then was also the registration key, which uninstall deletes. | `installer_smoke.py` allows exactly these (commits 65aafb7, 8169735); anything else still fails |
| `setup.exe` didn't link with updater integration | `ChannelOverrideWorkItem` is compiled only for Google Chrome | Patch 0023 compiles it whenever the flag is on |
| Install modes didn't compile with updater integration | With the flag, `ChannelStrategy` is `FLOATING` or `FIXED`; `UNSUPPORTED` exists only without it | Ghost's one mode is `FIXED` (commit eceaa2e) |
| The scrubber didn't compile | `base/containers/contains.h` is gone at 152 | `std::ranges::contains` (commit f16b9a8) |

**Other findings:**
- **Upstream's CUP keys** stay compiled in, unused (`std::ignore`), so that the yearly key rotation upstream makes in `request_sender.cc` keeps merging cleanly.
- **The updater's `LegalCopyright`** shows `@LASTCHANGE_YEAR@` in development builds, where `LASTCHANGE` isn't stamped. Official builds (sub-project E) stamp it.
- **The component updater also defaults to Google's publisher proof.** Sub-project C must switch it to `CRX3_WITH_GHOST_PUBLISHER_PROOF` for Ghost's components, and keep the Web Store's format for extensions.

## Mutation checks

Run 2026-10-03. Each change was made, the affected input rebuilt, the end-to-end test run in a fresh Sandbox, and the change undone. Patches were reverted in the checkout only (`git show <commit> -- <path> | git apply -R`, then `git checkout -- <path>`), and the scripts that changed the test's inputs lived outside the repository, so `tools/` was not edited.

| Change | Rebuilt | Failed |
|---|---|---|
| The server's CUP key replaced by a fresh one | nothing | **update:** `pv` stayed `…14901`. The updater rejected every response (`RequestSender failed -10000`) and downloaded nothing. |
| `update.crx3` re-signed with RFC 6979's test key | the CRX3 | **update:** `pv` stayed `…14901`. The updater downloaded the package, verified it and ran no installer. |
| Patch 0021 reverted (the scrubber's call in `protocol_serializer_json.cc`) | the static updater (38 s), the offline installer | **privacy:** all 3 requests carried `hw`, `dedup`, `domainjoined`, and per app `installdate` and `ping`; some `installsource` and `lang` |
| Patch 0022 reverted (`ping_manager.cc`) | the static updater (27 s), the offline installer | **privacy**, after a fix to the test: 3 of 8 requests were event requests (below) |
| Patch 0016's key path reverted (Google's path in `install_modes.cc`) | `mini_installer` at respin `-1` (8 min 35 s), the offline installer | **install:** `Clients\{appid} pv` missing; setup registered under `Software\Google\Update` |

**The test missed patch 0022 at first.** With the patch reverted the updater sent five event requests, and the test passed: the scrubber removes each app's `event` list, so they reached the server as apps without `updatecheck`, and the check looked only for an `events` key. `update_smoke.py` now requires every request to be an update check: an app without `updatecheck` fails it. Re-run, the mutation failed in **privacy**. The unmutated installers then passed every step again on 2026-10-03, with 3 requests, all update checks. A tooling test covers the case.

The scrubbed event requests carried nothing beyond an update check's allowed keys, but their existence reports that an install or update happened; the patch stays.

After the checks, `out/vanilla` and `out/updater` were rebuilt from the unmutated checkout at the development version.

## A real update request

The request the browser's update check sent in the passing run, from the server's log, is in [`test/updater/captured_request.json`](../../../test/updater/captured_request.json) and published in [privacy-model.md](../../privacy-model.md#the-update-request). It holds nothing to scrub: no install ID, no counters, and the two IDs are random per request and per update session.

## Audit of `USE_GOOGLE_UPDATE_INTEGRATION`

Patch 0016 turns the flag on, so that setup registers the browser with Ghost's updater. Every non-test use of the flag in `.cc`, `.h` and `.gn` files was read, together with what it compiles in. The flag moves the browser's `ClientState` from `Software\Browser` to `Software\ProjectGhost\Update\ClientState\{appid}`, the key the updater reads. The values below are read or written there.

**Nothing it compiles in sends data.** Each path is kept:

| Path | What it does | Why it's harmless |
|---|---|---|
| `install_static/install_modes.cc` | `Clients`, `ClientState` and `ClientStateMedium` under the updater's company key | Ghost's own key (patch 0016). It is the registration itself. |
| `install_static/install_util.cc` `DetermineChannel` | reads `ap` and `cohort\name` from `ClientState`, and a `channel` override from `Clients` | Local reads. The channel is fixed (`branding/install_modes.h`). The update request may carry `ap` (allow-list); cohorts are dropped by the scrubber. |
| `installer/setup/install_worker.cc` | creates `ClientStateMedium` for system installs; writes a policy-chosen channel into `ap` (`ChannelOverrideWorkItem`, patch 0023) | Local writes. Ghost installs per-user, and nothing chooses a channel by policy. |
| `installer/util/per_install_values.cc` | per-install values under `ClientState\PerInstallValues` | Local. Only the root key changes with the flag. |
| `browser/chrome_browser_main_win.cc`, `app/main_dll_loader_win.cc` (`kShouldRecordActiveUse`) | writes `dr` = 1 ("did run") at each browser start and each new renderer | The updater turns `dr` into the `ping` object's active counters, and the scrubber drops `ping` whole (patch 0021). |
| `browser/chrome_browser_main.cc` (`kShouldRecordActiveUse`) | writes `lastrun` (the last start time) | Local. Nothing in the updater sends it. |
| `browser/metrics/google_update_metrics_provider_win.cc` | adds the updater's state and the hashed cohort to UMA | Returns before reading anything in a build without Google Chrome branding. |

**Values the flag makes the updater see.** These are written by the browser or the installer whatever the flag, but with the flag they land where the updater reads:

| Value | Who reads it | Result in Ghost |
|---|---|---|
| `usagestats` (written when the user changes "help improve" in settings) | the updater: crash upload, remote event logging, app command pings, the enterprise companion's usage stats | Every one is empty or off: `crash_upload_url` and `updater_event_logging_url` are `""` (`branding/updater.gni`); app command pings are event requests, which `PingManager::SendPing` no longer sends (patch 0022); the enterprise companion runs only for cloud-managed installs. |
| `usagestats`, for the browser's own crash reporting (`install_static::GetCollectStatsConsent`) | Crashpad | `CrashReporterClient::GetUploadUrl` is empty without Google Chrome branding, so crash reports stay on disk. |
| `brand`, `reactivationbrand` | `google_brand::GetBrand` (RLZ, Google search's `brand` term) | Never written: the offline installer's arguments carry no `brand`, and RLZ is not built. The request's `brand` key stays absent. |
| `lang` | `chrome_feature_list_creator.cc`, RLZ | Never written by Ghost's installer. The scrubber drops `lang` from requests. |

**Google paths read whatever the flag.** Found by the same search for `Software\Google`; they read but send nothing, and are listed for the final-name step:
- `GoogleUpdateSettings::GetDownloadPreference` reads `HKLM\SOFTWARE\Policies\Google\Update\DownloadPreference` for the component and extension updaters' `dlpref`. An administrator's Google policy can make those requests prefer cacheable URLs; nothing else follows from it.
- `InstallerCrashReporterClient::ReportingIsEnforcedByPolicy` reads `SOFTWARE\Policies\Chromium`, while the browser's `install_static` reads `SOFTWARE\Policies\ProjectGhost\Browser`. Both only gate crash reporting, which has no upload URL.
- `upgrade_detector.cc` asks `versionhistory.googleapis.com` when the running Chromium release was last served, but only when the `RelaunchFastIfOutdated` policy is set. Sub-project A listed this call for B: it does not involve the updater, and the Chromium release is the right version to ask about.

## The updater's UI strings

The updater's strings come from `chrome/app/chromium_strings.grd` (`grdfile_name` in `branding/updater.gni`). Of the 62 en-US strings in the generated `updater_installer_strings.rc`, four name Chromium. **None shows during a per-user install:**

| String | Shown | Decision |
|---|---|---|
| `IDS_FRIENDLY_COMPANY_NAME` ("Chromium") | the installer window's title, only when the install arguments carry no `appname` | The offline installer passes `appname=Project Ghost`, so the title is "Project Ghost Installer". Final-name step. |
| `IDS_UPDATER_SERVICE_DISPLAY_NAME`, `IDS_INTERNAL_UPDATER_SERVICE_DISPLAY_NAME`, `IDS_UPDATER_SERVICE_DESCRIPTION` | the Windows services a system install registers | Ghost installs per-user. Final-name step. |

No en-US string names Google.
