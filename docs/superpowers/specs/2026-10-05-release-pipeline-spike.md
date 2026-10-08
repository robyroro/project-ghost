# Release pipeline: progress notes

- Phase 2, sub-project E ([design](2026-10-05-release-pipeline-design.md), [plan](../plans/2026-10-05-release-pipeline.md))
- Machine: the reference machine (Ryzen 5 3600, 6 cores, 32 GB), `autoninja -j 10`
- Chromium 152.0.7977.149, the 24-patch series, `build/args/release.gn` with `ghost_signing_identity = "test"`

## Spike, 2026-10-05

| Question | Answer |
|---|---|
| PGO profiles | Chrome's `win64` profile, 478 MB (100 MB compressed), from `chrome/build/win64.pgo.txt`; V8's builtins profiles, 18.8 MB for four files, `v8/tools/builtins-pgo/profiles/` |
| First official build: actions | 84,488 scheduled at the start; siso's estimate fell as it learned the graph: 32,313 done on day one, 29,396 on day two (the development build: about 81,000) |
| First official build: time | **13 h 16 min** in two parts: 4 h 52 min on 2026-10-05, stopped cleanly with Ctrl+C at the user's request, and 8 h 24 min on 2026-10-06, resumed where it stopped. Exit 0, nothing failed. |
| The pace | About 6,000 actions an hour at the start, 3,000–4,000 through Blink and V8, 5,000 near the end; 25 links, the slowest 7 min 27 s |
| Memory | Peak commit 45.4 GB; available memory fell to **0.8 GB** during `chrome.dll`'s ThinLTO link (2026-10-06 15:25). It fit, with no room to spare: nothing else should run beside an official link on 32 GB. |
| `out/release` | 35.7 GB |
| Free disk | 90.5 GB before, 46.5 GB after |
| `mini_installer.exe` | 123 MB (a component build's: 462 MB) |
| `ghost_unittests` in `out/release` | 27 of 27 pass (one more than in the development build: the test identity's own test) |
| `ghost_browsertests` in `out/release` | **Not built there.** siso scheduled 29,978 actions, then about 14,500 once started (the browser's test support code, then a ThinLTO link): four hours or more at the measured pace, over the plan's three-hour limit. The build was stopped cleanly after a minute. |
| `ghost_browsertests` in `out/vanilla` | Built in 6.7 min (incremental); 15 of 15 pass in 20 s. `release.py` builds and runs it there (`BROWSERTESTS_OUT`), and the provenance says so. |

**Findings so far:**
- **A respin of an official build recompiled most of the browser.** The first run of `release.py` (`152.0.7977.149-1`, 2026-10-06) scheduled about 91,000 actions after `release_version.py write`. siso's log gave the reason: `in:chrome/VERSION > out:…mojom-forward.h`. Official builds turn on `enable_mojom_message_id_scrambling`, whose salt is the contents of `//chrome/VERSION`; a new `PATCH` changes every method ordinal, so every generated mojom header, and everything that includes one. A development build doesn't scramble, which is why its respins took 7 minutes. The run was stopped cleanly after 23 minutes (4,727 steps, 0 failed). `build/args/release.gn` now sets `mojom_message_id_salt_path` to `build/mojom_message_id_salt`, a byte-for-byte copy of upstream's `chrome/VERSION` at the pin, which `release.py` checks; the mojom generator rewrites a file only when its contents change, so headers made with that salt match the first official build's.
- **Release `-1` failed the egress audit** (2026-10-07). Its build, `ghost_unittests`, `ghost_browsertests` and the Sandbox install passed; the audit counted 36 requests to `update.googleapis.com`. They came from the Optimization Guide's on-device model component, which registers after startup: `--disable-component-update` only skips `RegisterComponentsForUpdate`, and the component updater kept Google's URLs for anything registered later. The development build's audits had never shown it. Patch 0025 gives the component configurator no update or ping URLs while the switch is present (`component_update_urls_unittest.cc`); the development build's audit then counted 0 unexpected hosts. Tag `-1` stays as the failed attempt; `-2` is the fixed commit.
- **A respin still rebuilt about 18,000 actions,** five hours, after the salt fix. Release `-2` (2026-10-07) scheduled about 90,000 actions, which siso pruned to 18,166. `out/release/siso_explain` showed two ways `chrome/VERSION` reached every host tool (`protoc`'s plugins, `nasm`, the DXC and V8 generators, `torque`, `transport_security_state_generator`, …). A relinked tool reruns, so everything it writes is regenerated, and everything that includes that is recompiled: 1,014 dependents of V8's `bytecodes-builtins-list.h`, 2,400 of a Perfetto `.gen.h`.
  - **The build timestamp.** On Windows, upstream's `build/compute_build_timestamp.py` adds `chrome/VERSION`'s `PATCH` to an official build's timestamp, so that respins don't collide on a symbol server. The timestamp is in every link's `/TIMESTAMP` (611 links), base's build date header, DevTools' generated CSS (93 steps) and the installer's archive. `build/args/release.gn` now names `build/compute_build_timestamp.py`, which adds the Chromium release's `PATCH` instead: the same value as upstream's at `PATCH=149`, so the first build after it changes nothing. Respins of one release share a link timestamp; Ghost runs no symbol server.
  - **`//base`.** Two `//base` actions read `chrome/VERSION` as a file: `check_version_internal`, for `MAJOR`, and `version_info`, whose values patch 0012 already overrides. A rewrite reruns both, their headers get new timestamps, and `base.lib` is rebuilt, so every host tool on it is relinked. Patch 0026 takes both actions' values from `//ghost/build/version.gni` (the Chromium release); the generated headers are byte for byte the same. A development build doesn't show this: `base` is a DLL there, and the tools link its import library, which doesn't change.
  - Because `release.py` restores `chrome/VERSION` when a build stops, every stop and resume paid part of it again.
  - **Checked without an official build.** `gn gen` of a scratch directory with the release arguments, at upstream's `chrome/VERSION` and at `-2`'s: with both fixes, 26 of 13,706 ninja files differ, all `//remoting` (not built for a release: `out\release` has no `//remoting` object) plus the version resources' and updater's actions in `toolchain.ninja`. With upstream's timestamp script (mutation check), 635 differ: the 611 links, the 93 DevTools steps, base's build date. `gn refs //chrome/VERSION` lists no `//base` target; what still reads it are version resources, `chrome/common`'s version header, the updater's and the installer's.
  - **Measured in `out\updater`** (static, like `out\release`, but not official, so it shows only the `//base` path), with `chrome/VERSION` rewritten as `release.py` does: with patch 0026, 114 actions in 2.8 min, none in `//base` or a host tool. With it reverted (mutation check), 146 actions in 3.3 min: `base.lib`, then `transport_security_state_generator`, `root_store_tool`, `signer_set_tool` and `tag_exe` relinked and rerun.
  - The first build after patch 0026 reruns the two actions once: 10 min in `out\vanilla`, 3 min in `out\updater`, and in `out\release` about what `-2` was going to cost. After that, a respin should cost its version resources and headers plus `chrome.dll`'s ThinLTO link; the next release run measures it.
- **Ctrl+C leaves `autoninja.bat` waiting** at "Terminate batch job (Y/N)?" after siso stopped, and `release.py` waiting on it. Ending that `cmd` let `release.py` finish its cleanup: `chrome/VERSION` was restored and the build stage not recorded.
- **An official x64 build needs two of DEPS's `checkout_pgo_profiles` hooks,** not one: Chrome's `win64` profile, and V8's builtins profiles (`v8/tools/builtins-pgo/download_profiles.py`). Without the second, siso stops at once: `x64-rl.profile`, "missing and no known rule to make it". `release.py`'s sync stage runs both.
- **A build started from a tool's PowerShell dies with it.** `Start-Process` children belong to the tool's process tree, which ends with the command. A long build is started through WMI (`Win32_Process.Create`), outside it.
- **Windows PowerShell 5.1 turns a native program's stderr into errors.** With `$ErrorActionPreference = "Stop"`, `autoninja … 2>&1` ended the script on siso's first stderr line. `cmd /c "… > log 2>&1"` does the redirection instead.
- **The end-to-end test's Python lacked cffi.** The sandbox maps only the base Python installation; pip had put `cryptography`'s `cffi` in the user's site-packages, so the update server's service failed to import in the sandbox (`No module named '_cffi_backend'`). It failed before the block that writes the result file, and the host waited its whole hour. `update_smoke.py` now checks, before starting a sandbox, that the base Python imports the service (`python -I`), and reports a server that can't start as the run's error. The fix on this machine: `python -s -m pip install --no-user cffi==2.0.0 pycparser==3.0` (without `-s`, pip sees the user's copy and installs nothing).

## Releases, 2026-10-08

Patch 0025 (late component checks) and the respin fixes (patch 0026, the build timestamp) came after `-1` and `-2`, so the releases this plan names `-1` and `-2` are `-3` and `-4`, both on `547fd5e`. `-1` failed the egress audit; `-2` was stopped twice and never finished.

| Stage | `152.0.7977.149-3` | `152.0.7977.149-4` (respin) |
|---|---|---|
| Build | 5 h 42 min, about 31,000 actions: the one-time rerun after the respin fixes; `chrome.dll`'s ThinLTO link about 40 min | **23 min**, 664 actions: version headers and resources, the updater, the installer, `chrome.dll`'s link. `siso_explain` names nothing in `//base` and no host tool. |
| Test | 15 min: 27/27, 15/15, Sandbox install PASSED, egress audit 0 unexpected | 14 min, the same results |
| Sign | 7 min | 3 min 20 s |
| Describe, draft | 1 min | 1 min |
| Whole run | about 6 h 5 min | **41 min** |

- `release.py verify` passed for both; the drafts' asset digests match the local files.
- **End to end, `-3` → `-4`** (2026-10-08, 3 min 21 s in Windows Sandbox, the update server's own service on loopback): clean machine, trust the signing certificate, install, signatures, halted (fraction 0), rolled out (fraction 1), signatures after the update, launch, privacy, uninstall: all ok, PASSED.

## Mutation checks

Each change made its check fail, and was undone before the next (2026-10-08, against `-3` and `-4`).

| Change | Result |
|---|---|
| A local change in the Chromium checkout (`README.md`) | `release.py run --tag 152.0.7977.149-4` stopped at the checks: "…\src has local changes, which no release may build", exit 1. (It also named this repository being past the tag's commit, which was true.) |
| A server that ignores the fraction (`Offer.choose` offers always) | The end-to-end test, `-3` → `-4`: "halted (fraction 0)" FAILED, "the server offered 152.0.7977.14904" and "pv is '152.0.7977.14904', expected '152.0.7977.14903'", exit 1 |
| A wrong hash in the provenance (a copy of `-4`'s files) | `verify`: "provenance.intoto.json: its SHA-256 differs from SHA256SUMS" and "ProjectGhostOfflineSetup.exe: the provenance names another SHA-256", exit 1 |
| `--public` with the test identity | "the test identity never makes a public release (docs/licensing.md#release-gates)", exit 2 |
