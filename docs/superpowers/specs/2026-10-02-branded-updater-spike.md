# Branded updater: spike results

- Date: 2026-10-02 to 2026-10-03
- `CHROMIUM_VERSION`: 152.0.7977.149
- Design: [2026-10-02-branded-updater-design.md](2026-10-02-branded-updater-design.md)

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
