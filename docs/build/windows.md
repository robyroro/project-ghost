# Building on Windows

This guide covers the Windows 11 x64 build of the pinned Chromium version with Ghost on top. The requirements come from `build/requirements.json`, which is derived from the pinned tag's own build instructions. When `CHROMIUM_VERSION` changes, re-read this guide against the new tag.

## Hardware

| | Minimum | Recommended |
|---|---|---|
| CPU | x86-64 | 16+ cores (upstream: "20+ is not excessive") |
| RAM | 8 GB | 64 GB; official (ThinLTO) links need more than 32 GB |
| Disk | 100 GB free, NTFS or ReFS | 250 GB+ on a TLC NVMe drive, formatted as a Dev Drive |

### Measured

First full build of vanilla Chromium 152.0.7977.140 (`chrome` target), measured 2026-09-29:

| | |
|---|---|
| Machine | Ryzen 5 3600 (6 cores / 12 threads), 32 GB RAM, 1 TB QLC NVMe (NTFS, not a Dev Drive) |
| GN args | `is_debug=false is_component_build=true symbol_level=1 blink_symbol_level=0 v8_symbol_level=0` |
| Wall time | **11 h 17 min** (677 min) |
| `out/vanilla` size | 21.2 GB |
| Checkout (shallow dependencies, full-history `src` at the tag) | ~30 GB |

**Notes on the measurement**
- The Defender exclusion for the checkout was added about 7.5 hours in. The compile rate for comparable Blink files rose from ~25 to ~39 files per minute afterwards, so a build with the exclusion from the start will be noticeably faster.
- The CPU was at 100% throughout. On this machine the build is CPU-bound, so the next lever is more cores.
- This is the cost of a **full** build: the first build, and each move to a new milestone. Incremental builds after editing a few files recompile only those files and relink.

## Prerequisites

Some of these need administrator rights or change system security settings. The developer performs them. The tools only check.

1. **Build volume.**
   - A directory with no spaces in its path.
   - Not inside OneDrive or another synced folder.
   - At least 250 GB free.
   - A Dev Drive (ReFS) performs best.
2. **Visual Studio 2022 or 2026** (any edition, including Build Tools), with:

   ```
   --add Microsoft.VisualStudio.Workload.NativeDesktop --add Microsoft.VisualStudio.Component.VC.ATLMFC --includeRecommended
   ```

   For Build Tools, the equivalent components are `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` and `Microsoft.VisualStudio.Component.VC.ATLMFC`.
3. **Windows SDK 10.0.26100.7705**, including **Debugging Tools for Windows** (10.0.26100.3323 or newer). To add Debugging Tools to an installed SDK: Settings → Apps → Windows Software Development Kit → Modify.
4. **Environment variable** `DEPOT_TOOLS_WIN_TOOLCHAIN=0`, set as a user variable. Without it, depot_tools tries to download Google's internal toolchain and the build hooks fail.
5. **Git settings**, as required by upstream:

   ```
   git config --global core.autocrlf false
   git config --global core.filemode false
   git config --global core.preloadindex true
   git config --global core.fscache true
   git config --global branch.autosetuprebase always
   git config --global core.longpaths true
   ```
6. **Microsoft Defender exclusion** for the build root. Scanning makes builds dramatically slower. This is a security setting: decide it yourself, and scope it to the build directory only.

## Check the machine

```
python tools/check_env.py --build-root D:\ghost
```

Every `FAIL` must be fixed before building. Each line comes with the fix.

**A subtle case:** `check_env.py` reproduces Chromium's own Visual Studio discovery (`build/vs_toolchain.py`).
- That script looks only in `%ProgramFiles%\Microsoft Visual Studio\{18,2022}\<edition>`, trying 2026 before 2022, or in the `vs2026_install` / `vs2022_install` variables.
- Build Tools 2022 installs under `Program Files (x86)` by default, so the build can't see it even though the Visual Studio Installer shows it as installed.
- `check_env.py` reports this and tells you which variable to set.

## Create the checkout

```
python tools/bootstrap.py --root D:\ghost --dry-run     # show the exact commands
python tools/bootstrap.py --root D:\ghost
```

`bootstrap.py` does five things:
1. clones depot_tools, unless `--depot-tools` points at an existing copy;
2. writes `.gclient` with two solutions (`src` and `src/ghost`);
3. checks that the `CHROMIUM_VERSION` tag points at `CHROMIUM_COMMIT`;
4. syncs Chromium **by commit hash** and runs the hooks;
5. hides `src/ghost` from Chromium's `git status`.

**How the tag is checked.**
- **An existing checkout** fetches the tag shallowly before the sync. The fetch brings in the new commit.
- **A new checkout** is cloned at the hash. It then asks `origin` where the tag points, using `git ls-remote`, and records the tag locally.
- Fetching the tag into a new checkout would download the whole clone a second time (1.4 GB at 152). On Windows that fetch then fails with `Permission denied`: git can't replace the identical pack it already has, which is read-only.

**Why sync by hash rather than by tag.** gclient skips its "fetch every upstream branch" step only when the requested revision is a hash that is already present locally. Given a tag, it always runs that fetch. On a shallow checkout, the fetch can go for hours without output, and gclient reports `STALL DETECTED` on `src`.

`src/ghost` is cloned from this repository's `origin`, or from the local checkout if there's no remote. From then on it is the working copy for Ghost development.

Pass `--pgo` only on machines that produce official builds; it downloads the PGO profiles.

### Adopting an existing Chromium checkout

If Chromium was already fetched with `fetch chromium`, point `--root` at the directory that contains `.gclient`, and `--depot-tools` at the existing depot_tools.
- Bootstrap rewrites `.gclient` to add the `src/ghost` solution.
- It moves `src` to the pinned tag without re-downloading history.
- A full-history checkout (a plain `fetch chromium`) is the right kind for the machine that does milestone moves.

If `fetch` ended with a hook failure about the Windows toolchain, set `DEPOT_TOOLS_WIN_TOOLCHAIN=0` in a new terminal and run `gclient runhooks`. The sync itself was not affected.

## Build

Development builds use `build/args/dev.gn`: a component build with reduced symbols and Ghost's branding. Apply the patch series first ([patching.md](../patching.md)).

```
cd D:\ghost\src
python ghost\tools\patches.py apply --src .
gn gen out\vanilla --args="import(\"//ghost/build/args/dev.gn\")"
autoninja -C out\vanilla chrome ghost_unittests ghost_browsertests
```

The output directory keeps the name of the first baseline build, `out\vanilla`, because renaming one discards the build state (see Troubleshooting). On 32 GB machines, add `-j 10` to full builds.

## Installer

```
autoninja -C out\vanilla mini_installer
python ghost\tools\installer_smoke.py sandbox --installer out\vanilla\mini_installer.exe
```

- After `chrome` is built, `mini_installer` takes about 2 minutes on the reference machine.
- A development installer is large: 643 MB, from a 1.7 GB `chrome.7z`. A component build packs every DLL and uses fast compression.
- The smoke test installs, checks, launches and uninstalls in a fresh Windows Sandbox with no network, and prints each step's result. What it checks is in [testing.md](../testing.md).
- **Windows Sandbox** needs Windows 11 Pro or Enterprise with virtualization enabled in the firmware. Enabling it is a system change for the developer to make, as an administrator, followed by a restart:

  ```
  Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM
  ```

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `STALL DETECTED` on `src` during `gclient sync`, no output for many minutes | gclient was given a tag or branch, so it fetches every upstream branch into a shallow checkout | Stop it and use `tools/bootstrap.py`, which syncs by commit hash |
| `cipd ensure` fails on `chromium/third_party/updater/chrome_win_*`: "the file contains a virus or potentially unwanted software" | Microsoft Defender flags the old Chrome installers that `//chrome/updater` integration tests use as test data | Allow the detection in Defender, or leave those dependencies out with `custom_deps` set to `None` in `.gclient` (they are needed only for updater tests) |
| `LLVM ERROR: out of memory` on ordinary files | The system commit limit (RAM + page file; 41.9 GB on the 32 GB reference machine) was reached by parallel compiles, plus anything else running | Re-run the same command; it resumes. Use `autoninja -j 10` on 32 GB machines, don't compile anything else alongside a full build, or enlarge the page file. |
| Everything recompiles after renaming or moving an `out\` directory | Siso records build state under the output directory's path. A renamed directory looks brand new, and the old state is discarded, even if you rename it back | Never rename or move an output directory. Create a new one with `gn gen` and accept a full build, or keep the name. |
| `No supported Visual Studio can be found` | VS not in a location `vs_toolchain.py` searches | Set `vs2022_install` / `vs2026_install` to the install path (`check_env.py` prints it) |
| A hook tries to download a toolchain and gets access denied | `DEPOT_TOOLS_WIN_TOOLCHAIN` unset | Set it to `0`, open a new terminal, run `gclient runhooks` |
| Errors mentioning `atlbase.h` or MFC headers | ATL/MFC component missing | Add `Microsoft.VisualStudio.Component.VC.ATLMFC` in Visual Studio Installer |
| Can't read large PDBs in the debugger | Debugging Tools too old | Install Debugging Tools 10.0.26100.3323 or newer |
| Scripts fail with `\r` errors, or patches don't apply | `core.autocrlf=true` when the checkout was made | Set `core.autocrlf=false` and re-checkout the affected files |
| Very slow builds | Defender scanning the build tree, or a QLC SSD under sustained writes | Add the Defender exclusion; use a faster drive or a Dev Drive |
