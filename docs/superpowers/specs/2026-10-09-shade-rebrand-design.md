# The product name, Shade: design

- Status: design approved 2026-10-09
- Phase 2, done before sub-project F ([roadmap](../../roadmap.md#phase-2-release-engineering)): F's first security release is the first official build under the final name
- Depends on sub-projects B ([branded updater](2026-10-02-branded-updater-design.md)), D ([signing](2026-10-04-signing-design.md)) and E ([release pipeline](2026-10-05-release-pipeline-design.md))

## Goal

The browser installs, runs and updates as **Shade**. Everything a user or a tester sees outside the browser's own menus carries the new name and logo: the install path, the registry, shortcuts, the taskbar, Apps & features, file associations, the installers, the updater and the release's assets. The Windows identity is renamed while it still costs nothing: no public build exists, so no install has to be migrated.

## Decisions

These were settled in discussion on 2026-10-09:

- **The product is "Shade", everywhere Windows shows it:** shortcut, Start, taskbar, Apps & features, file properties, as with Brave or Firefox. "Shade Browser" is for the website and other material. (First approved as two names, "Shade" short and "Shade Browser" full; the shortcut and the Apps & features entry both come from `IDS_PRODUCT_NAME`, which the installer strings take from `PRODUCT_FULLNAME`, so they can't differ without another installer patch. Settled on one name on 2026-10-09.)
- **The domain is browseshade.com. It isn't bought yet,** so nothing depends on it working: no contact address and no update URL use it. It appears only in reverse-DNS identifiers (`com.browseshade.*`), which work without owning the domain but would have to change if someone else registered it. Buying it is a release gate.
- **Scope: the Windows identity, the installers, the updater, the icons and the release's names.** The browser's menus and dialogs still say "Chromium": renaming `chromium_strings.grd` touches hundreds of strings and every translation, and is a later sub-project. The logo with the "Chromium" wordmark (`product_logo_name_22`) goes with it.
- **The internal codename stays `ghost`,** as Chromium keeps `chrome/`: `//ghost`, the patches and their messages, `GHOST_INDEX`, the `Ghost*` features, GN arguments such as `ghost_update_url`, the tools' local directories (`~/ProjectGhostKeys`, `~/ProjectGhostReleases`) and the GitHub repositories (`project-ghost`, `project-ghost-update-server`). Users never see these. The repositories are renamed at the public launch, with the trademark gate; GitHub redirects the old names.
- **The test signing identity stays as it is.** The certificate "Project Ghost Test Code Signing" and the test CUP and publisher keys are throwaway: only test machines trust them. The production identity, made by the same ceremony at the end, is named Shade. A new test ceremony now would cost the PIN and a new backup and prove nothing new.
- **Every GUID and CLSID stays.** They are random, unique and unseen outside this project.
- **The logo comes from one source image, through a tool, into committed files** (approach 1 of three). The alternatives were generating icons during the build (every build would need Pillow, and nobody sees the result without building) and copying icons over `chrome/app/theme/chromium/` at apply time (breaks "every change through `git am`", makes `patches.py export` produce binary diffs that `check` rejects).
- **The logo is the user's own work,** made in Photoshop for this project; the user holds its rights. [licensing.md](../../licensing.md#assets) records this.
- **The source is the raster the user supplied** (1250 px WebP on white), until a larger or transparent export from the Photoshop file, or a vector master, arrives. Replacing `branding/logo/source.png` and running the tool again is the whole upgrade. At 16–32 px the white S-shaped gap is expected to blur; a simplified small variant can be added beside the source.
- **The trademark is not cleared.** A quick search on 2026-10-09 found no browser named Shade, but found a US application for "SHADES" (Shades Media, Inc., serial 97720908, filed 2022-12-16) covering downloadable software for accessing the internet, plus nearby names (a tab-masking extension called Shade, ShadeYouVPN, NetShade). The name gate in [licensing.md](../../licensing.md#release-gates) stays open until a trademark attorney's search (USPTO and EUIPO) clears it. Until the first public build, renaming again costs only this sub-project's work.

## The names

| Field | Value | Where it shows |
|---|---|---|
| `PRODUCT_SHORTNAME`, `base_app_name` | Shade | StartMenuInternet key, taskbar |
| `PRODUCT_FULLNAME` | Shade | Shortcut name and Apps & features (both `IDS_PRODUCT_NAME`), file properties |
| `COMPANY_SHORTNAME`, `kCompanyPathName`, `updater_company_short_name` | Shade | `%LOCALAPPDATA%\Shade\Browser`, `Software\Shade\{Browser,Update}` |
| `COMPANY_FULLNAME`, `updater_company_full_name` | Shade | File properties only; becomes the legal entity's name when there is one |
| `PRODUCT_INSTALLER_FULLNAME`, `_SHORTNAME`, `updater_metainstaller_name` | Shade Installer | UAC prompt, file properties |
| `COPYRIGHT`, `updater_copyright` | The Shade Authors | File properties |
| `base_app_id` (AppUserModelID) | Shade | Groups the browser's windows on the taskbar |
| ProgID prefixes | `ShadeHTM`, `ShadePDF` | File associations (within the 12-character limit) |
| ProgID descriptions | Shade HTML Document, Shade PDF Document | "Open with" |
| `direct_launch_url_scheme` | `shadebrowser` | Direct launch; "shade" alone is too generic and could collide |
| `MAC_BUNDLE_ID` | `com.browseshade.browser` | Not compiled on Windows |
| `MAC_CREATOR_CODE` | `Shde` (four characters, as the field requires) | Not compiled on Windows |

The updater, in `branding/updater.gni`:

| Field | Value |
|---|---|
| `browser_name`, `browser_product_name` | Shade |
| `crash_product_name`, `updater_product_full_name` | ShadeUpdater |
| `updater_product_full_display_name` | Shade Updater |
| `updater_product_full_name_dashed_lowercase` | shade-updater |
| `updater_company_short_name_lowercase`, `_uppercase` | shade, SHADE |
| `keystone_app_name` | ShadeSoftwareUpdate |
| `privileged_helper_bundle_name` | ShadeUpdaterPrivilegedHelper |
| Mac bundle identifiers | `com.browseshade.Keystone`, `com.browseshade.Browser`, `com.browseshade.ShadeUpdater`, `com.browseshade.Browser.UpdaterPrivilegedHelper` |
| `legacy_service_name_prefix` | `shadeupdate` |

The release:

| Field | Value |
|---|---|
| Offline installer | `ShadeSetup.exe` (was `ProjectGhostOfflineSetup.exe`) |
| Draft title and notes | `Shade <version>` |
| SBOM package name | `Shade` (`SPDXRef-Package-Shade`) |

## Components

| Unit | What it does |
|---|---|
| `branding/BRANDING`, `install_modes.h`, `updater.gni` | The names above. `updater.gni` loses its "TEST IDENTITY" comment: the names are now final, the GUIDs always were. |
| `branding/logo/source.png` | The source image, the user's WebP converted to PNG losslessly. |
| `branding/logo/small.png` | Optional. Used for sizes up to 32 px when present. |
| `tools/brand_icons.py` | Generates every icon from the source into `branding/theme/` (below), and with `--preview <dir>` a contact sheet. Removes generated files it no longer makes. Uses Pillow. |
| `branding/theme/` | The generated icons, committed. |
| `tools/pe_resources.py` | Reads the icon groups out of a PE file (`RT_GROUP_ICON` and its `RT_ICON` entries), standard library only. |
| `tools/installer_smoke.py` | Also checks the new names and the icons (Testing). |
| `tools/offline_installer.py`, `release.py`, `sbom.py` | The release's names. |
| `tools/requirements.txt` | Adds `Pillow==12.2.0`, the version on the reference machine. CI already installs this file (for `cryptography`). |
| Patch 0027 | Points Chromium's icon references at `branding/theme/` (below). |

### What the tool generates

1. **Background.** If the source has no transparency, white connected to the image's border becomes transparent; along that boundary, anti-aliased edge pixels get partial alpha with their colour un-mixed from white, so no white fringe remains. The S-shaped gap opens to the outside at both ends, so it becomes transparent too: it is negative space in the logo. White enclosed by the logo, such as highlights on the sphere, stays opaque.
2. **Framing.** Crop to the logo, centre it on a square, add a small margin.
3. **Resizing.** Always from the full-resolution image, with Lanczos.
4. **Outputs:**
   - `win/app.ico`: 16, 20, 24, 32, 40, 48, 64 and 256 px; the 256 px image stored as PNG. The browser's executable, shortcuts and taskbar, and the icon of `mini_installer`, `setup` and the updater's installer and UI.
   - `win/doc.ico`, `win/pdf.ico`: the logo on a stylised page, at the same sizes.
   - `win/tiles/Logo.png` and `win/tiles/SmallLogo.png`, at upstream's pixel sizes and proportions: 600 px with the logo 220 px wide, and 176 px with the logo 118 px wide, centred.
   - `default_100_percent/product_logo_16.png`, `product_logo_32.png` and their `default_200_percent` and `default_300_percent` versions (2× and 3×).
   - `product_logo_16.png`, `_24`, `_64`, `_128`, `_256.png`: the unscaled logos and the profile shortcut badges.
   - `product_logo.svg`: the 256 px PNG inside an SVG, for the WebUI's `IDR_PRODUCT_LOGO_SVG` and `IDR_PRODUCT_LOGO_ANIMATION_SVG` (a still logo; there's no animation to make).
   - Upstream's incognito and app-list icons are generic (a hat and glasses, a grid), so they stay. Upstream's installer and updater icons are generic boxes; they become the Shade icon, as Chrome's installer shows Chrome's.
5. **Contact sheet** (`--preview`, never committed): every size on a light and a dark background, at 100% and enlarged, for judging legibility at 16 px.

File names are product-neutral (`app.ico`, not `shade.ico`), so a later rename touches none of them. The tool is deterministic for a given source and Pillow version.

### Patch 0027

"theme: take the product icons from //ghost/branding/theme". Text only, so `patches.py check` accepts it. It redirects:

- `chrome/app/chrome_exe.rc`: the application, HTML and PDF document icons in the non-Google branch.
- `chrome/app/chrome_dll.rc`: `IDR_MAINFRAME`.
- `chrome/app/theme/theme_resources.grd`: `IDR_PRODUCT_LOGO_16` and `IDR_PRODUCT_LOGO_32` on Windows. grit looks a scaled image up as `default_<scale>_percent/<file>`, so one path can't name three scales; each gets an `<if expr="context == 'default_<scale>_percent'">` branch with a path into `branding/theme/default_<scale>_percent/`.
- `chrome/app/theme/chrome_unscaled_resources.grd`: on Windows, `IDR_PRODUCT_LOGO_64`, `128`, `256`, `IDR_PRODUCT_LOGO_SVG`, `IDR_PRODUCT_LOGO_ANIMATION_SVG` and `IDR_PRODUCT_LOGO_{16,24,64,128}_SHORTCUTS`.
- `chrome/BUILD.gn`: the tiles. (Its other `product_logo_32` is macOS's.)
- `chrome/installer/mini_installer/mini_installer.rc`, `chrome/installer/setup/setup.rc`, `chrome/updater/win/installer/installer.rc`, `chrome/updater/win/ui/resources/resources_en.rc`.

`Why:` Chromium picks its icons by `branding_path_component`, with no argument for another brand. `Upstream:` not upstreamable: product-specific branding.

**Deliberately unchanged:**

- `kCurrentProfileIconVersion` (`chrome/browser/profiles/profile_shortcut_manager_win.cc`). Chromium's README asks for an increment when the icon changes; it migrates profile shortcuts of existing installs, and there are none.
- The Start tile's background colour (`#5F6368`, `generate_visual_elements_manifest_work_item.cc`). Windows 11's Start shows no tiles; on Windows 10 the logo reads well on grey.

## For the spike

Answered while planning (2026-10-09), by reading the sources:

- The Windows build's logo resources are the ones listed under Patch 0027; upstream's incognito and app-list icons are generic.
- `rc` reports icon files through `/showIncludes`, so a changed icon rebuilds its binary.
- The update server doesn't read the updater's name; its and webops' captured request fixtures (`"@updater": "ProjectGhostUpdater"`) record traffic of their time and stay.

Left for the first build with patch 0027, recorded in `2026-10-09-shade-rebrand-spike.md`: whether grit's `context` branches select the right file per scale (checked in the built `.pak` files), and whether `rc` resolves the paths into `//ghost`.

## Error handling

- `brand_icons.py` fails, writing nothing, when the source is missing or smaller than 256 px on its shorter side, and when the background step leaves no opaque pixel or leaves an opaque pixel on the border.
- `pe_resources.py` raises on a PE without a resource section or without the requested icon group, naming the file; callers report it as a failed check, not a crash.
- A smoke check that finds an old name reports the name and where it found it.

## Testing

**Unit tests:**

- `brand_icons.py`: the regeneration test generates in memory and compares with `branding/theme/`: the same set of files, each PNG's decoded pixels, each ICO's images (BMP entries byte for byte, PNG entries decoded) and the SVG's embedded PNG, decoded. Pixels rather than file bytes, because Pillow's PNG compression may differ between platforms. Without Pillow the test fails; it isn't skipped. Plus tests of the background step on small synthetic images: border white removed, enclosed white kept, edge pixels un-mixed.
- `pe_resources.py`: builds a minimal PE in memory with an icon group, on any OS.
- A branding test: no working name (`Project Ghost`, `ProjectGhost`, `projectghost`) in `branding/BRANDING`, `install_modes.h` or `updater.gni`; `kCompanyPathName` equals `updater_company_short_name`, and `app_guid` equals `browser_appid`.
- `installer_strings`, `installer_smoke`, `update_smoke`, `release` and `sbom` expect the new names. `update_smoke` takes the updater's directory and task name from `updater.gni` instead of a literal.

**`installer_smoke`, in Windows Sandbox, on a real install,** also checks:

- the install path `%LOCALAPPDATA%\Shade\Browser` and the keys `Software\Shade\{Browser,Update}`;
- the "Shade" shortcut and the "Shade Browser" entry in Apps & features;
- the `ShadeHTM` and `ShadePDF` ProgIDs and the `shadebrowser` scheme;
- that every image in the installed `chrome.exe`'s main icon group equals, byte for byte, the corresponding image in `branding/theme/win/app.ico`, and that `chrome.exe` also carries `doc.ico` and `pdf.ico`.

The installed `setup.exe` and the installer under test are checked the same way (their first icon group).

**Mutation checks**, each must fail:

- M1: drop one redirection from patch 0027 (the main icon in `chrome_exe.rc`); `installer_smoke` fails on the icon.
- M2: change the source without regenerating; the regeneration test fails.
- M3: leave `ProjectGhost` as `kCompanyPathName`; the branding test fails. (`installer_smoke` reads its expectations from `branding/`, so it would follow the mistake.)

**End to end, on the development builds:**

- `out/vanilla` and `out/updater` rebuilt incrementally;
- `ghost_unittests`, `ghost_browsertests`, the tooling tests and `tools/lint.py`;
- `installer_smoke`; `update_smoke` (install, update, recovery, uninstall) with the updater's new names;
- the egress audit: no unexpected host.

**On the host,** `tools/pe_resources.py check-icon` checks the offline installer and `updater.exe`.

**By the user:** the contact sheet, then an install in Windows Sandbox: taskbar, Start, Apps & features, file associations.

No official release is cut for this sub-project. F's `152.0.7977.158-1` is the first official build named Shade; the drafts `152.0.7977.149-3` and `-4` keep the old name.

## Documentation

- `branding/README.md`: the name is final; the icons and the tool; how to replace the source.
- [licensing.md](../../licensing.md#trademarks): "Shade" chosen; the clearance search pending, with the "SHADES" application to check; buying browseshade.com before the first public build. The release gate stays open. Under [Assets](../../licensing.md#assets): the logo was drawn for this project by its author, who holds its rights.
- [architecture.md](../../architecture.md), [windows.md](../../build/windows.md), [release.md](../../build/release.md), [privacy-model.md](../../privacy-model.md): the new names, paths and installer name.
- [roadmap.md](../../roadmap.md): this sub-project, before F.
- Earlier specs, plans and the 2026-10-04 ceremony record keep the old name: they record what was true then.

## Done when

- [ ] Every check under Testing passes and the three mutation checks fail as required.
- [ ] The user has approved the contact sheet and the installed browser's look.
- [ ] The documentation above is updated.
- [ ] webops (and the update server, if its fixture changes) pushed, tooling CI green on Ubuntu and Windows.

## Out of scope

- "Chromium" in the browser's menus, dialogs and translations, and `product_logo_name_22`: a later sub-project.
- Icons for Linux, macOS and ChromeOS.
- Renaming the GitHub repositories, the codename and the test signing identity.
- Buying the domain, contact addresses on it, and the update server's URL on it (sub-project C's deployment).
- Trademark clearance.
