# Release version: design

- Status: implemented 2026-10-02 ([spike results](2026-10-02-release-version-spike.md))
- Phase 2, sub-project A
- Implements [ADR 0007](../../adr/0007-version-numbers.md)

## Context: Phase 2 in sub-projects

Phase 2 ([roadmap](../../roadmap.md)) is split into sub-projects. Each gets its own design, plan and implementation, in this order:

| | Sub-project | Depends on |
|---|---|---|
| **A** | Release version: ADR 0007's scheme in builds | — |
| **B** | Branded `//chrome/updater`: our app IDs and server, installed with the browser | A |
| **C** | Update server: Omaha 4, CUP-signed responses, no IP retention, components later | B is tested against it |
| **D** | Signing: Authenticode, the CUP key, the CRX3 key, and where the keys live | — |
| **E** | Release pipeline: official build from a tag, SBOM and provenance, staged rollout with a halt switch | A–D |
| **F** | Security release runbook, and one measured milestone move (to 154) | E |

**Phase 2 runs under a test identity.**
- The final product name isn't chosen ([licensing.md](../../licensing.md#trademarks)). The name must be final before a build creates app IDs or update endpoints that real installs depend on.
- Phase 2 therefore builds and tests end to end with test app IDs, a test server domain and test keys, on test machines only.
- Moving to the final name is one isolated step, through `branding/` and the server configuration, before the first public build.

## Goal

A release build carries the release version (`152.0.7977.14901`) wherever Windows, the installer and the updater look. Everything a website or Google sees keeps showing Chromium's version (`152.0.7977.149`). This holds with no audit of call sites when a milestone moves.

## Two sources of truth

Both files already exist:

- **`CHROMIUM_VERSION`** (this repository) is the Chromium release: `152.0.7977.149`.
- **`chrome/VERSION`** (upstream) is the release version.
  - Development builds keep upstream's: `PATCH=149`.
  - Release builds get `PATCH = 149 × 100 + respin` from the release tool.

## The cut: at `version_info`, not at the call sites

**Chromium builds `version_info` in one place.** `//base/version_info:generate_version_info` runs `build/util/version.py` over `//chrome/VERSION`. Everything in the browser that reports the version to a website or to Google gets it from `version_info`:
- the User-Agent;
- Client Hints (`full_version`, `brand_full_version_list`);
- the Chrome Web Store's `prodversion`;
- extensions' `minimum_chrome_version`.

**The installer and the file resources read `//chrome/VERSION` separately**, in more than 20 build files. So do `chrome/common`'s version header and the updater.

**The hook is one patch to `base/version_info/BUILD.gn`.**
- It passes `-e MAJOR=… -e MINOR=… -e BUILD=… -e PATCH=…` to `generate_version_info`. `version.py` applies `-e` values after reading `//chrome/VERSION`, so `version_info` gets the Chromium release.
- `//ghost/build/version.gni` builds those arguments from `CHROMIUM_VERSION` at `gn gen` time. Nothing is written to disk.
- In development builds both versions are equal, so the hook changes nothing visible.

**Rejected: patching each place that sends the version out.**
- `version_info::GetVersionNumber()` has 76 callers in browser code at 152. Every milestone would need an audit of them.
- A missed call site sends the respin to websites: a fingerprint. Nothing breaks when that happens, so nobody would notice.
- Cutting at the source fails the other way. A place that needs the release version and doesn't get it shows a visible bug, such as an update prompt that never goes away. It never leaks.

## Who sees which version

| Reader | Version | Through |
|---|---|---|
| UA, Client Hints, Chrome Web Store, `minimum_chrome_version`, every other `version_info` caller | Chromium: `152.0.7977.149` | the hook |
| Installer, `setup.exe`, install directory, registry `pv`, Apps & features, file version resources, updater | release: `152.0.7977.14901` | `chrome/VERSION`, unpatched |
| Ghost code that needs the release version: the About page, the upgrade detector | release | `//ghost/version`, below |

**`//ghost/version`** is a new target.
- It runs its own `process_version` over `//chrome/VERSION` and generates `ghost::ReleaseVersion()`, a `base::Version`.
- `ghost::DisplayVersion()` formats it the way ADR 0007 shows people a version: `152.0.7977.149-1`, and no suffix for respin 0.

**Places inside the browser that compare its running version with the installed one must use `ghost::ReleaseVersion()`.**
- The upgrade detector is one. `InstalledVersionPoller` compares the installed version with `version_info::GetVersion()`. Left alone, it would see an update on every launch.
- The spike finds the others, including any code that builds paths to `<install>\<version>\`.

## The release tool

`tools/release_version.py` is stdlib-only and tested, like the rest of `tools/`.

**The release tag is the source of truth.**
- Release tags in this repository are `<CHROMIUM_VERSION>[-<respin>]`.
- `152.0.7977.149` is respin 0. `152.0.7977.149-1` is respin 1.

**`compute --tag <tag>`** prints the release version: `152.0.7977.14901` for `152.0.7977.149-1`. It refuses:
- a tag whose Chromium part isn't `CHROMIUM_VERSION`;
- an upstream PATCH above 655, or a respin of 100 or more, which would overflow the 16-bit version field (ADR 0007);
- `-0`. Respin 0 has no suffix, so every release has exactly one spelling.

**`write --src <src> --tag <tag>`** rewrites only the `PATCH=` line of `chrome/VERSION`.
- It refuses unless `chrome/VERSION` equals `CHROMIUM_VERSION` before the write.
- So it can't run twice, or on a checkout at another pin.

**Only the release pipeline (sub-project E) calls it**, after `patches.py apply` and before `gn gen`. Development builds and `builder.py` (`pr`, `nightly`) never touch `chrome/VERSION`.

## Display

- **`chrome://settings/help` and `chrome://version`** show `ghost::DisplayVersion()` (`152.0.7977.149-1`). That takes one small hook where the help page builds its version string; the spike locates it.
- **Windows shows the release version** (`152.0.7977.14901`) in file properties and Apps & features. That needs no patch.

## Errors caught at `gn gen`

`//ghost/build/version.gni` asserts:
- that `chrome/VERSION` and `CHROMIUM_VERSION` have the same MAJOR, MINOR and BUILD;
- that `chrome/VERSION`'s PATCH either equals `CHROMIUM_VERSION`'s (a development build) or lies in `[PATCH × 100, PATCH × 100 + 99]` (a release build).

A mismatched checkout then stops at `gn gen` with a message naming both versions, not at install time.

## The spike

The spike runs in the reference checkout (`out/vanilla`) with a fake respin, before anything is committed as final.

1. **Build.** Apply the `version_info` hook and `//ghost/version`, and run `release_version.py write --tag 152.0.7977.149-1`. Then build `chrome` and `mini_installer` incrementally.
2. **Install in Windows Sandbox and check:**
   - the registry, the install directory and Apps & features show `152.0.7977.14901`;
   - the User-Agent, Client Hints and the Chrome Web Store's `prodversion` show `152.0.7977.149`;
   - no "relaunch to update" prompt appears;
   - the About page shows `152.0.7977.149-1`.
3. **Audit** the places in the browser that compare `version_info` with an installed version, or build paths with it.
   - The list goes into the spike notes.
   - Each entry gets a fix, or a stated reason it doesn't matter.
4. **Measure the respin rebuild.**
   - With the hook, a respin changes only what reads `chrome/VERSION` directly: resources, `chrome/common` and the installer. The spike measures the rebuild that causes.
   - It then puts `chrome/VERSION` back.

## Tests

- **`tools/tests/test_release_version.py`** covers:
  - tag parsing;
  - the limits;
  - `write` and its refusals.
- **`ghost_unittests`** covers `ReleaseVersion()` and `DisplayVersion()`, including no suffix for respin 0.
- **`ghost_browsertests`** checks that a page sees `CHROMIUM_VERSION` in the User-Agent and in `navigator.userAgentData.getHighEntropyValues(["fullVersionList"])`.
  - Where the release version differs from `CHROMIUM_VERSION`, as in the spike and in every release build, it also checks that the page never sees the release version.
  - Its mutation check runs once, in the spike: without the hook the test must fail. Removing the hook recompiles everything that includes `version_info`. That cost is accepted, because this test carries A's privacy guarantee.
- **`installer_smoke.py`** takes the expected version from the installer's file version instead of `CHROMIUM_VERSION`.

## Done when

- The spike's installer passes the Windows Sandbox smoke test with the release version.
- The checks in the spike pass, and the browser test is mutation-checked.
- Respins 0 and 1 of a Chromium release order correctly, below the next upstream release.
- ADR 0007 gets an implementation note: the cut is at `version_info`.
- The roadmap gets a Phase 2 section listing sub-projects A to F.

The series is expected to grow by two or three hook patches: `version_info`, the help page, and possibly the upgrade detector.

## Out of scope

- The updater, the server, signing and the pipeline (sub-projects B to E).
- Versions on macOS and Linux. Windows is the only target until Phase 9.
