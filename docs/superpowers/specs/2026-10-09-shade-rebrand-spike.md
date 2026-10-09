# The product name, Shade: progress notes

- Phase 2, before sub-project F ([design](2026-10-09-shade-rebrand-design.md), [plan](../plans/2026-10-09-shade-rebrand.md))
- Machine: the reference machine (Ryzen 5 3600, 6 cores, 32 GB)
- Chromium 152.0.7977.149, the series with patch 0027 (27 patches), development builds `out/vanilla` (component) and `out/updater` (static)

## Settled while planning, 2026-10-09

By reading the sources, before any build:

| Question | Answer |
|---|---|
| Which logo resources the Windows build uses | `IDR_MAINFRAME` and the document icons in `chrome_exe.rc`, `IDR_MAINFRAME` in `chrome_dll.rc`, one icon each in `mini_installer.rc`, `setup.rc`, the updater's `installer.rc` and `resources_en.rc`; the Start tiles (`chrome/BUILD.gn`); `IDR_PRODUCT_LOGO_16` and `_32` (scaled), `_64`, `_128`, `_256`, the two SVGs and the four shortcut badges (unscaled). The `product_logo_32` in `chrome/BUILD.gn` is macOS's; the status tray icon is macOS's. |
| Upstream's icons that stay | Incognito (a hat and glasses) and the app list (a grid): generic. Upstream's installer, setup and updater icons are generic boxes; they become Shade's. |
| Does a changed icon rebuild its binary? | Yes: `rc` reports icon files through `/showIncludes`, so ninja tracks them. |
| Does the update server read the updater's name? | No. Its fixture and webops' captured requests carry `"@updater": "ProjectGhostUpdater"`; they record traffic of their time and stay. |
| One name or two? | One. The shortcut and the Apps & features entry both show `IDS_PRODUCT_NAME`, which the installer strings take from `PRODUCT_FULLNAME`, so "Shade" short and "Shade Browser" full can't coexist without another installer patch. The user chose "Shade" everywhere. |

## The first build with patch 0027

| Question | Answer |
|---|---|
| Does grit take a scaled image from `//ghost`? | Yes, with one `<if expr="context == 'default_<scale>_percent'">` branch per scale. grit joins the scale directory and the file path, so a single relative path would have given every scale the same image. Checked in the packs: each scale's `product_logo_16.png` and `_32.png` is byte for byte in `chrome_100_percent.pak` and `chrome_200_percent.pak`. |
| The unscaled logos | `product_logo_16`, `_24`, `_64`, `_128`, `_256.png` byte for byte in `resources.pak`. The SVG is there gzipped (grit compresses SVG by default) and stored once: both its resource IDs point at the same data. |
| Does `rc` find `ghost/branding/theme/win/app.ico`? | Yes: paths from the source root resolve through the include path, as `resources_en.rc` already did. `pe_resources.py check-icon` finds `app.ico` as the first icon group of `chrome.exe`, `mini_installer.exe`, `setup.exe`, the offline installer, `UpdaterSetup.exe` and `updater.exe`. |
| Build | `out/vanilla`: 398 actions, 8 min 17 s (the names in `install_modes.h` and the updater's branding header recompile their includers, then the component DLLs relink). `out/updater`: 2 min 9 s. A respin of `mini_installer`: about 3 min. |

## The logo

- The user's file was already transparent (1254 px, RGBA), so `brand_icons.py` didn't have to remove a white background. The S-shaped gap is transparent and shows the background through it.
- Nearly every logo pixel has alpha 253, likely Photoshop's layer opacity on export. Invisible in practice; a new export at 100% opacity fixes it.
- At 16–24 px on a dark background, the logo's dark navy upper half merges into the background. A simplified `branding/logo/small.png` for up to 32 px is the remedy, at any time.
- The user approved the contact sheet on 2026-10-09.

## End to end, 2026-10-09

| Check | Result |
|---|---|
| Tooling tests | 436 pass (1 skipped: the TPM test) |
| `ghost_unittests`, `ghost_browsertests` | 27 of 27; 15 of 15 (the two known `EXCESSIVE_OUTPUT` retries) |
| `installer_smoke` | PASSED in 1 min 36 s, with the new "installer icon" step. Installed at `…\AppData\Local\Shade\Browser`; Apps & features "Shade", publisher "Shade"; `Shade.lnk` in Start and on the Desktop; `ShadeHTM`, `ShadePDF`, `shadebrowser`. |
| `update_smoke` | PASSED in 4 min 6 s: install from `ShadeSetup.exe`, update `-1` to `-2`, launch, privacy, uninstall |
| Egress audit | 0 unexpected hosts (one route probe and the `wpad` lookup, reported, not counted) |

**Found by the end-to-end run:** the first `update_smoke` run never reported. `offline_installer.py` read the product's name from `branding/BRANDING` at import, and Windows Sandbox maps only `tools/`, so the Sandbox side crashed on import, before writing a result, and the host waited (24 minutes, stopped by hand). Fixed in `df80a2a`: `offline_installer.output_name()` reads it when called, the host passes the name to the Sandbox as it always did, and a test imports the Sandbox side from a copy of `tools/` with no `branding/` beside it (it failed before the fix with the same `FileNotFoundError`).

## Mutation checks

| Mutation | Expected | Result |
|---|---|---|
| M1: `chrome_exe.rc`'s `IDR_MAINFRAME` back to `chromium.ico` | `installer_smoke` fails on the icon | FAILED: "chrome.exe's icon is not branding/theme/win/app.ico" (2 min 51 s build, 1 min 35 s test). Restored; `check-icon` ok again. |
| M2: one pixel of `branding/logo/source.png` changed, nothing regenerated | the regeneration test fails | FAILED on every output (`win/app.ico`, `doc.ico`, `pdf.ico`, the tiles, …). Restored. |
| M3: `kCompanyPathName` back to `ProjectGhost` | the branding test fails | FAILED: `test_no_working_name_is_left` and `test_the_updater_lives_in_the_browsers_company_directory`. Restored. |

## Corrections to the plan

- `patches.py check` takes no `--src`.
- Editing files through Python in a Bash heredoc turned `\\` into `\` twice (a test string, a docs table of Windows paths); those edits were redone with the editor. Known from earlier sessions; keep backslash edits out of heredocs.
