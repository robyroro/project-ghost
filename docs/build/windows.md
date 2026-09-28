# Building on Windows

This guide covers the Windows 11 x64 build of the pinned Chromium version with Ghost on top. The requirements come from `build/requirements.json`, which is derived from the pinned tag's own build instructions. When `CHROMIUM_VERSION` changes, re-read this guide against the new tag.

## Hardware

| | Minimum | Recommended |
|---|---|---|
| CPU | x86-64 | 16+ cores (upstream: "20+ is not excessive") |
| RAM | 8 GB | 64 GB; official (ThinLTO) links need more than 32 GB |
| Disk | 100 GB free, NTFS or ReFS | 250 GB+ on a TLC NVMe drive, formatted as a Dev Drive |

Time and disk use for the first full build on our reference machine will be recorded here after Phase 1's baseline build.

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
3. fetches the `CHROMIUM_VERSION` tag shallowly, and checks that it points at `CHROMIUM_COMMIT`;
4. syncs Chromium **by commit hash** and runs the hooks;
5. hides `src/ghost` from Chromium's `git status`.

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

GN argument files and build targets arrive in Phase 1. Until then, the upstream baseline build is:

```
cd D:\ghost\src
gn gen out\vanilla --args="is_debug=false is_component_build=true symbol_level=1 blink_symbol_level=0 v8_symbol_level=0"
autoninja -C out\vanilla chrome
```

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `STALL DETECTED` on `src` during `gclient sync`, no output for many minutes | gclient was given a tag or branch, so it fetches every upstream branch into a shallow checkout | Stop it and use `tools/bootstrap.py`, which syncs by commit hash |
| `cipd ensure` fails on `chromium/third_party/updater/chrome_win_*`: "the file contains a virus or potentially unwanted software" | Microsoft Defender flags the old Chrome installers that `//chrome/updater` integration tests use as test data | Allow the detection in Defender, or leave those dependencies out with `custom_deps` set to `None` in `.gclient` (they are needed only for updater tests) |
| `No supported Visual Studio can be found` | VS not in a location `vs_toolchain.py` searches | Set `vs2022_install` / `vs2026_install` to the install path (`check_env.py` prints it) |
| A hook tries to download a toolchain and gets access denied | `DEPOT_TOOLS_WIN_TOOLCHAIN` unset | Set it to `0`, open a new terminal, run `gclient runhooks` |
| Errors mentioning `atlbase.h` or MFC headers | ATL/MFC component missing | Add `Microsoft.VisualStudio.Component.VC.ATLMFC` in Visual Studio Installer |
| Can't read large PDBs in the debugger | Debugging Tools too old | Install Debugging Tools 10.0.26100.3323 or newer |
| Scripts fail with `\r` errors, or patches don't apply | `core.autocrlf=true` when the checkout was made | Set `core.autocrlf=false` and re-checkout the affected files |
| Very slow builds | Defender scanning the build tree, or a QLC SSD under sustained writes | Add the Defender exclusion; use a faster drive or a Dev Drive |
