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

**Findings so far:**
- **An official x64 build needs two of DEPS's `checkout_pgo_profiles` hooks,** not one: Chrome's `win64` profile, and V8's builtins profiles (`v8/tools/builtins-pgo/download_profiles.py`). Without the second, siso stops at once: `x64-rl.profile`, "missing and no known rule to make it". `release.py`'s sync stage runs both.
- **A build started from a tool's PowerShell dies with it.** `Start-Process` children belong to the tool's process tree, which ends with the command. A long build is started through WMI (`Win32_Process.Create`), outside it.
- **Windows PowerShell 5.1 turns a native program's stderr into errors.** With `$ErrorActionPreference = "Stop"`, `autoninja … 2>&1` ended the script on siso's first stderr line. `cmd /c "… > log 2>&1"` does the redirection instead.
