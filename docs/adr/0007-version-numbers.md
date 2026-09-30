# 0007. Version numbers

- Status: Proposed
- Date: 2026-09-30

## Context

Ghost ships on top of Chromium's Extended Stable releases ([ADR 0003](0003-upstream-extended-stable.md)). Sometimes it must also ship without a new one, for example a Ghost-only fix between two upstream security releases.

Four readers of the version number want different things. Measured on 152.0.7977.140:

- **The installer and the updater** need a number that grows with every Ghost release.
  - Chromium's installer takes its version from `chrome/VERSION`. So do the directory it installs into, the `pv` registry value, the Apps & features entry and the Windows file version resources.
  - It treats an equal version as a repair and refuses a lower one.
  - More than 20 build files read `//chrome/VERSION` directly.
- **Websites and Google services** should see exactly what the same Chromium release shows. A version of our own would single out Ghost users.
  - The reduced User-Agent (`Chrome/152.0.0.0`) uses only MAJOR.
  - These carry the full `version_info::GetVersionNumber()`:
    - high-entropy Client Hints (`full_version`, `brand_full_version_list`);
    - the full User-Agent, if the reduction feature is switched off on the command line (the `UserAgentReduction` policy ended with M144);
    - extension update checks to the Chrome Web Store (`prodversion`).
- **Code that compares against Chromium versions.** An extension's `minimum_chrome_version` is compared with the full browser version (`MinimumChromeVersionChecker`). A MAJOR.MINOR.BUILD below upstream's would refuse extensions written for this Chromium release.
- **People** need to tell releases apart and to know which Chromium they run.

Windows version resources hold 16 bits per component, so each part is at most 65535.

The roadmap's first sketch kept `chrome/VERSION` upstream and gave the installer, updater and About page a separate product version, `<major>.<chromium major>.<release>`. But the installer and updater read `chrome/VERSION` itself. Every place they take the version from would need a patch. Without those patches, a Ghost-only release on an unchanged Chromium version would look to them like a repair of the installed one.

## Decision

- **Release version.** Every Ghost release has a four-part version: Chromium's MAJOR, MINOR and BUILD, and a fourth part equal to `PATCH × 100 + respin`.
  - `PATCH` is upstream's patch number. `respin` counts the Ghost releases made on that Chromium release, starting at 0.
  - Chromium 152.0.7977.140 ships as **152.0.7977.14000**. A Ghost-only fix on top of it is **152.0.7977.14001**. The next upstream release, say 152.0.7977.160, ships as **152.0.7977.16000**.
  - Release builds write this version into `chrome/VERSION`. The installer, the updater and the version resources then use it with no patch.
  - The release tooling refuses an upstream PATCH above 655, or a 100th respin, because either would overflow the 16-bit field.
- **The web and Google see Chromium's version.**
  - Every place the full version reaches a website or a Google service reports `CHROMIUM_VERSION` instead: Client Hints, the unreduced User-Agent, and Chrome Web Store update checks.
  - A browser test checks that what a page sees equals `CHROMIUM_VERSION` while `chrome/VERSION` differs from it.
- **People see both parts.** The About page and release notes write the version as the Chromium release, a hyphen, and the respin: `152.0.7977.140-1`. Respin 0 has no suffix. This is the convention Linux distributions use for package revisions.
- **Development builds** keep upstream's `chrome/VERSION`.
- **When:** this lands with the release pipeline in Phase 2, before the first release. Until then no build carries a version different from upstream's, so there is nothing to hide from the web.

## Consequences

- **Updates always move forward.**
  - A respin raises only the fourth part.
  - A new Chromium patch release raises the fourth part by at least 100.
  - A new milestone raises BUILD and MAJOR.
- **Comparisons against Chromium versions stay correct.** MAJOR.MINOR.BUILD equal upstream's, and the fourth part is never below upstream's PATCH, so any minimum stated for this Chromium release is met.
  - The exception: a minimum naming a *later* patch of the same build also passes. It matters only to an extension that names a specific patch release.
  - The Chrome Web Store sees `CHROMIUM_VERSION`, so it decides compatibility exactly.
- **Windows file properties and Apps & features show the release version** (152.0.7977.14001). The About page spells out what it means.
- **What we maintain:**
  - release tooling that writes `chrome/VERSION` and checks the limits;
  - a hook that reports `CHROMIUM_VERSION` to the web and to the Chrome Web Store, with its test;
  - the About page display.
- **If upstream PATCH ever exceeds 655** within one milestone, this scheme has to change. At the pin it is 140.
- **The version still says "Extended Stable"** to websites, as [ADR 0003](0003-upstream-extended-stable.md) already accepts. It says nothing more.

## Alternatives considered

- **Keep `chrome/VERSION` upstream and add a product version** (the roadmap's first sketch). It needs patches wherever the installer, the updater and more than 20 build files read the version. Without them, respins are impossible.
- **Chromium's MAJOR, then a product version** (`152.1.4.2`, Brave's scheme). Numbers grow and read well. But MINOR and BUILD no longer match upstream's. With a product major of 0, as ours is before 1.0, `152.0.4.2` sorts below `152.0.7977`, so extensions written for this Chromium release would be refused. It also needs the same hook for the web.
- **Ship only on upstream releases.** No scheme is needed, but every Ghost fix, including one for a Ghost security bug, waits for Google's next release of the milestone.
- **Report the release version to the web.** Every respin would carry a version no Chrome has, and a site could tell Ghost users apart by it.
