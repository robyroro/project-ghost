# Branded updater: design

- Status: implemented 2026-10-03 ([spike results](2026-10-02-branded-updater-spike.md))
- Phase 2, sub-project B ([roadmap](../../roadmap.md#phase-2-release-engineering))
- Depends on sub-project A ([release version](2026-10-02-release-version-design.md))

## Goal

Ghost installs, updates and uninstalls through Chromium's `//chrome/updater`, branded as Ghost, per-user, under Phase 2's test identity. An update is applied end to end in Windows Sandbox from a CUP-signed response. The update request carries only what [privacy-model.md](../../privacy-model.md#data-the-browser-sends) allows.

## Decisions

These were settled in discussion on 2026-10-02:

- **Installation is Chrome's model.** The metainstaller (`UpdaterSetup`) installs the updater, then the browser.
  - The **offline** variant carries the browser installer inside it. B builds and tests this one.
  - The **online** variant downloads the browser from the server. It comes with sub-project C.
- **Per-user scope only.** Per-system installs (Program Files, a Windows service, elevation) become their own sub-project, before the public alpha.
- **B ends with an applied update.** A test server signs its responses with CUP using a test key. It proves the whole client side: CUP with our key, the request contents, installation of the update. C replaces the test server with the real one.
- **A company level above the browser, as Google and Brave have.**
  - Uninstalling the browser deletes the contents of its registry key (`RemoveDistributionRegistryState` in `chrome/installer/setup/uninstall.cc`). With Phase 1's layout, the browser's key is `Software\Project Ghost`, so an updater key beneath it would be wiped while the updater is still installed.
  - The browser therefore moves under a company directory, and the updater sits beside it:

| | Phase 1 | Sub-project B |
|---|---|---|
| `kCompanyPathName`, `kProductPathName` | `""`, `Project Ghost` | `ProjectGhost`, `Browser` |
| Browser files | `%LOCALAPPDATA%\Project Ghost\Application` | `%LOCALAPPDATA%\ProjectGhost\Browser\Application` |
| User data | `%LOCALAPPDATA%\Project Ghost\User Data` | `%LOCALAPPDATA%\ProjectGhost\Browser\User Data` |
| Browser registry | `Software\Project Ghost` | `Software\ProjectGhost\Browser` |
| Apps & features key | `Project Ghost` | `Project Ghost Browser` |
| Updater files | — | `%LOCALAPPDATA%\ProjectGhost\<updater name>\<version>` |
| Updater registry, and the browser's registration | — | `Software\ProjectGhost\Update`, `…\Update\Clients\{appid}` |

  Only test installs exist, so this is the cheapest moment for the move. The final name will need the same structure. Shortcuts and the product name people see stay `Project Ghost`. The company directory has no space: upstream's `uninstall.cmd`, which removes the updater, can't handle one (found by the end-to-end test; see the [spike results](2026-10-02-branded-updater-spike.md)).
- **Updates carry Ghost's publisher proof.**
  - On Windows an update is a CRX3 that wraps the browser installer (Omaha 4's `download` then `crx3` operations).
  - The production updater requires a publisher proof (`CRX3_WITH_PUBLISHER_PROOF`), a second signature by a key whose hash the client pins. Upstream pins Google's key. The same constant verifies Chrome Web Store extensions, so it stays as it is.
  - A new verifier format, `CRX3_WITH_GHOST_PUBLISHER_PROOF`, accepts only Ghost's publisher key, and the updater requires it.
  - **Why it matters:** the CUP key signs each response live, so it sits on the server. The publisher key signs packages and is kept offline (sub-project D decides where). A compromised server therefore cannot publish an update. This is the mitigation [threat-model.md](../../threat-model.md) names: "Offline and HSM-held signing keys".
  - In B the publisher key is a test key, which also serves as the CRX's developer key.
- **CUP signs with ECDSA.**
  - `request_sender.cc` also carries a post-quantum ML-DSA-44 key, chosen when the `PqcCupSigning` feature is on. The feature is off by default at 152.
  - The patch pins ECDSA with Ghost's key, so an upstream change of that default cannot make every update fail. Post-quantum CUP is decided with the production keys in sub-project D.

## What upstream provides, and what it assumes

Measured at 152.0.7977.149:

- **`//chrome/updater` supports embedders other than Google.** But `chrome/updater/branding.gni` knows two brandings only:
  - Google Chrome;
  - "Chromium", whose defaults point at Google's QA update server (`omaha-qa.sandbox.google.com`) and send crash reports and event logs to Google.
- **The updater is built only for Chrome builds by default:** `enable_updater = is_chrome_branded` in `chrome/browser/buildflags.gni`, a GN argument.
- **The CUP public key is compiled into `components/update_client/request_sender.cc`**, one key for every embedder.
- **Where the browser installer registers with the updater:**
  - It writes the registration under `Software\Google\Update\Clients\{appid}`, hard-coded in `chrome/install_static/install_modes.cc`.
  - Only builds with `USE_GOOGLE_UPDATE_INTEGRATION` write it, and only `is_chrome_branded` builds set that flag.
  - Otherwise there is no registration.
- **The updater accepts `http://` update URLs.** Integrity comes from CUP, not TLS. URL overrides (`overrides.json`) exist only in the updater's test builds.
- **The offline metainstaller is built by `chrome/updater/win/signing/sign.py`.** It embeds `OfflineManifest.gup` and the app installer, and has `--disable_tag_and_sign` for unsigned builds.

## Components

### New in `//ghost`

**`branding/updater.gni`: Ghost's updater identity**, the values `branding.gni` otherwise takes from the Chromium branch.
- The company is `Project Ghost`, the same as the browser's `kCompanyPathName`, and the updater is `Project Ghost Updater`. The updater's files and registry therefore sit beside the browser's, as in the layout table above.
- **The GUIDs are newly generated:**
  - the browser's app ID and the updater's app ID;
  - the mutexes;
  - every COM class and interface GUID the Chromium branch defines.
  
  The file marks them as the **test identity**, to be replaced when the final name is chosen.
- `update_check_url = "http://127.0.0.1:8484/update"`, loopback only, for the test identity.
- **Nothing reaches Google:**
  - `crash_upload_url`, `updater_event_logging_url`, `app_logo_url` and `help_center_url` are empty;
  - `crx_pkhash` is empty until the updater updates itself through our server (sub-project C).

**`branding/crx_publisher_key.h`:** the SHA-256 of Ghost's CRX3 publisher key (DER SubjectPublicKeyInfo), in the form `crx_verifier.cc` compares. In B it is the test key's hash; sub-project D replaces it.

**`branding/cup_key.h`:** the CUP public key and its key version.
- In the test identity the key is a test key. Its private half is committed in `test/updater/`, marked as test-only.
- Sub-project D replaces it with the production key and decides where the private key lives.

**The request scrubber**, a Ghost hook in `components/update_client`. It applies an **allow-list** to the JSON that is actually sent, just before `ProtocolSerializerJSON::Serialize` writes it. The serializer always writes some objects, such as `hw` with zeros and `dedup`, whatever the request holds, so cleaning the C++ request would not be enough.

| Object | Keys kept |
|---|---|
| `request` | `protocol`, `ismachine`, `acceptformat`, `sessionid` (random per update session, never stored), `requestid` (random per request), `@updater`, `updaterversion`, `prodversion` (the Chromium release, through sub-project A), `updaterchannel`, `prodchannel`, `@os`, `arch`, `wow64`, `dlpref`, `os`, `apps` |
| `os` | `platform`, `arch`, `version` |
| each `app` | `appid`, `version`, `ap`, `brand`, `release_channel`, `enabled`, `disabled`, `cached_items`, `updatecheck`, `data` |
| `disabled` entries | `reason` |
| `cached_items` entries | `sha256` |
| `updatecheck` | `updatedisabled`, `rollback_allowed`, `sameversionupdate`, `targetversionprefix` |
| `data` entries | `name`, `index` |

- **Everything else is dropped**, including any key a later milestone adds upstream. Dropped today:
  - `iid` (the install ID) and `installdate`;
  - `ping`, with its `rd`, `ad`, `a`, `r` and `ping_freshness` counters;
  - `hw`, `lang`, `domainjoined`, `dedup` and `os.sp`;
  - `updaters`, the updater's own state with `lastchecked` and `laststarted`;
  - `installsource`, `installedby`, the cohorts, installer attributes, and the extra request attributes;
  - the text of `data` entries.
- **Event requests (`events`) are not sent.** `PingManager::SendPing` drops its events before anything else, so its existing "no events" branch returns without a request.
- **A dropped key never fails silently.** If one turns out to be needed, the tests show it. No new key can leave unnoticed: the same principle as sub-project A's cut at `version_info`.
- **The hook sits in `update_client`'s serializer**, so it covers:
  - the updater's checks for the browser;
  - the updater's own self-updates;
  - the browser's component updater when it returns in sub-project C.

**`tools/update_server.py`:** a stdlib-only Omaha 4 test server, tested like the rest of `tools/`.
- **`keygen`** generates the test CUP key pair. The private key goes to `test/updater/`; the public key goes to `branding/cup_key.h`, in the encoding `request_sender.cc` uses.
- **`crx`** packs an installer into a CRX3 signed with the test publisher key (ECDSA P-256, the same pure-Python code as CUP). The browser installer is its only file.
- **`serve`** listens on `127.0.0.1:8484`:
  - it answers update checks for the browser's app ID with an update when the request's version is older than the CRX it serves: a `download` operation (URL, size, SHA-256) then a `crx3` operation running `mini_installer.exe`;
  - it signs every response with CUP (ECDSA P-256 over the response body plus the request hash, sent in `X-Cup-Server-Proof`), in pure Python;
  - it logs every request body to a JSONL file;
  - it serves the installer files.
- **Its tests** check:
  - the CUP signature against a verifier written from [cup.md](https://chromium.googlesource.com/chromium/src/+/refs/tags/152.0.7977.149/docs/updater/cup.md);
  - the CRX3 layout and its signatures, against the format in `components/crx_file/crx3.proto`;
  - the response format;
  - the allow-list checker that the end-to-end test also uses.

**`tools/offline_installer.py`** wraps `sign.py --disable_tag_and_sign`.
- It packs `mini_installer.exe` as the browser's installer.
- It adds an `OfflineManifest.gup` with the release version, size and SHA-256.
- It writes `ProjectGhostOfflineSetup.exe`.
- Until sub-project D signs and tags installers, the install arguments (`appguid=…&appname=Project Ghost&needsadmin=False`) are passed on the command line.

**`tools/update_smoke.py`:** the end-to-end test in Windows Sandbox, reusing `installer_smoke.py`'s Sandbox mechanism. See [The end-to-end test](#the-end-to-end-test).

### Hook patches to Chromium

| File | Change |
|---|---|
| `chrome/updater/branding.gni` | Imports `//ghost/branding/updater.gni` instead of the Chromium values |
| `chrome/install_static/BUILD.gn` | Turns the installer's registration (`USE_GOOGLE_UPDATE_INTEGRATION`) on for Ghost builds |
| `chrome/install_static/install_modes.cc` | Registration under `Software\ProjectGhost\Update\…`, the updater's company path, instead of `Software\Google\Update\…` |
| `components/update_client/request_sender.cc` | The CUP key from `//ghost/branding/cup_key.h`, always ECDSA |
| `components/crx_file/crx_verifier.{h,cc}` | Adds `CRX3_WITH_GHOST_PUBLISHER_PROOF`, which accepts only the key in `//ghost/branding/crx_publisher_key.h` |
| `chrome/updater/external_constants_default.cc` | The updater requires `CRX3_WITH_GHOST_PUBLISHER_PROOF` |
| `components/update_client/protocol_serializer_json.cc`, `ping_manager.cc` | Call the scrubber; send no event requests |
| `chrome/browser/updater/browser_updater_client_win.cc` | Registers `ghost::ReleaseVersion()`: the requirement sub-project A's [audit](2026-10-02-release-version-spike.md#audit-of-version_info-callers) left for B |

`branding/install_modes.h` gets the browser's `app_guid`, empty since Phase 1 "until the updater exists", and the new `kCompanyPathName` and `kProductPathName`. `installer_smoke.py` follows: it currently refuses a non-empty company path, and learns the new install directory and Apps & features key.

**No patch is needed to build the updater.** `enable_updater` and `enable_update_notifications` are GN arguments (`chrome/browser/buildflags.gni`), so `build/args/dev.gn` sets both to `true`. The second turns on the browser's "relaunch to update" prompt, which the audit below also covers.

## Install, update, uninstall

**Install.**
1. `ProjectGhostOfflineSetup.exe` installs the updater per-user and registers its scheduled task.
2. The updater runs the browser installer from the offline payload.
3. `setup.exe` installs Ghost as today, and writes `Software\ProjectGhost\Update\Clients\{appid}` with `pv` set to the **release version** (sub-project A). It reports its result in `ClientState`.

**Update.**
1. The updater wakes from its scheduled task, or `updater.exe --wake` in the test.
2. It sends a scrubbed request, and receives a CUP-signed response naming the new `mini_installer.exe`.
3. It downloads the CRX3, checks its SHA-256 and Ghost's publisher proof, unpacks it and runs the installer inside.
4. `setup.exe` installs the new version beside the old one and updates `pv`. If the browser is running, upstream's `new_chrome.exe` mechanism swaps at the next launch.

**Uninstall.**
1. Apps & features runs `setup.exe`, which removes the browser and its `Clients` key.
2. At its next wake, the updater has no apps left and uninstalls itself.

## Audit of `USE_GOOGLE_UPDATE_INTEGRATION`

Turning the flag on for Ghost enables more than the registration keys. Examples are the `dr` ("did run") value, brand codes, and the `usagestats` consent read from `ClientState`.

The spike lists every code path the flag enables in the installer and the browser, as sub-project A's spike listed `version_info` callers. Each path is either:
- **turned off**, because it sends data or reaches Google;
- or **kept, with the reason it's harmless.**

## The end-to-end test

`tools/update_smoke.py sandbox` runs these steps in a fresh Windows Sandbox:

1. **Start the test server** with the respin `-2` installer.
2. **Run respin `-1`'s offline installer.** The checks are `installer_smoke.py`'s, with release version `…14901`, plus:
   - the updater is installed and its scheduled task registered;
   - `Clients\{appid}` has `pv` `…14901`.
3. **Run `updater.exe --wake`.**
   - `pv` must become `…14902`, and the `152.0.7977.14902` directory must exist.
   - The browser starts and reports `Chrome/152.0.7977.149` over DevTools.
4. **Check the server's request log** against the allow-list.
   - The updater's log must name no URL other than `127.0.0.1`.
5. **Uninstall the browser, then run `updater.exe --wake` again.**
   - The updater must have removed itself: no directory, no scheduled task, no registry keys.

**Inputs.** The test needs two respin builds, `-1` and `-2`, each with its installer copied out.
- Each build takes about 12 minutes on the reference machine (A's spike).
- `chrome/VERSION` is restored afterwards.
- Building the updater targets the first time adds an estimated 30 to 60 minutes.

## Tests

- **`ghost_unittests`:** the scrubber.
  - A request with every field set must serialize to exactly the allowed keys, compared with a golden file.
  - The test is mutation-checked.
- **`tools/tests`:** `update_server.py` (CUP, the response, the allow-list) and `offline_installer.py` (manifest contents).
- **Mutation checks of the end-to-end test.** Each of these must fail the test:
  - **The server signs with another CUP key.** The update is rejected and `pv` stays at `…14901`.
  - **The CRX is signed with another publisher key.** The update is rejected and `pv` stays at `…14901`.
  - **The scrubber hook is removed.** Forbidden fields appear in the server's log.
  - **The registration path patch is removed.** The updater doesn't find the browser.

## Done when

- `tools/update_smoke.py sandbox` passes:
  - install;
  - update from `-1` to `-2`;
  - uninstall, and the updater's self-removal.
- The update request holds only allowed fields, and the updater contacts nothing else.
- The `USE_GOOGLE_UPDATE_INTEGRATION` audit is written, with a decision for each path.
- The mutation checks above pass.
- The documentation is updated:
  - [privacy-model.md](../../privacy-model.md#data-the-browser-sends) publishes the exact request, as it promised for Phase 2: the allow-list, plus an example captured by the test server. It also says that `sessionid` is random per update session.
  - [architecture.md](../../architecture.md#updates-and-signed-data) describes the updater as built.
  - [build/windows.md](../../build/windows.md) explains how to build the offline installer.
  - The roadmap marks B done.

## Out of scope

- Per-system installs: their own sub-project.
- Signing and tagging: sub-project D.
- The real update server, the online installer, and the updater updating itself: sub-project C.
- An update applied while the browser runs, through `new_chrome.exe`: checked in sub-project C or E.
- macOS and Linux.
