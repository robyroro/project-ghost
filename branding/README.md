# branding/

Everything that names the product lives here. "Project Ghost" is a working name, so renaming the product means editing this directory and nothing else.

| File | What it controls | How the build uses it |
|---|---|---|
| `BRANDING` | Product and company name, copyright, in version resources and the About page | GN argument `branding_file_path` in `build/args/dev.gn`, no patch needed |
| `install_modes.h` | Windows identity: user data and registry path, app name and AppUserModelID, ProgIDs, URL scheme, Active Setup and COM class ids, sandbox AppContainer SID prefix | Included by `chrome/install_static/install_modes.h` in place of Chromium's header (patch) |

## Rules for changing values

- **Before the first public release**, the name and every identifier here can change freely.
- **After a public release**, changing `kProductPathName`, the app name or id, the ProgIDs or the COM class ids moves users' data and breaks existing registrations. That needs a migration plan.
- **ProgID prefixes must stay within 12 characters.** Windows limits ProgIDs to 39 characters, user-level installs append a 27-character suffix, and Chromium's installer stops on anything longer.
- **Leave `elevator_iid` and `tracing_service_iid` alone.** They must match the interface ids compiled from `chrome/elevation_service/elevation_service_idl.idl` and `chrome/windows_services/elevated_tracing_service/tracing_service_idl.idl`.
- **Generate new ids with a random (version 4) UUID generator.** Never copy one from another product.

Development builds show `Copyright @LASTCHANGE_YEAR@ …` in file properties. That's expected: non-official builds read `build/util/LASTCHANGE.dummy` (`use_dummy_lastchange = !is_official_build`), which has no year, so that commits don't force rebuilds. Official builds substitute the real year.

## Not here yet

- **Icons and logos:** Chromium's are used until the product has its own.
- **Product name in UI strings:** Chromium's `chromium_strings.grd` still says "Chromium" in menus and dialogs. That's a separate change, because it touches hundreds of strings.
