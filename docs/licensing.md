# Licensing

This document covers the license of this repository, the rules for bringing in anyone else's code or data, and the legal questions that must be settled before a public release. It is an engineering policy, not legal advice. Items marked as gates need review by counsel.

## Project license

- Code in this repository is licensed under the **Mozilla Public License 2.0** ([LICENSE](../LICENSE)). The reasoning is in [ADR 0002](adr/0002-license-mpl2-dco.md).
- Every source file we write starts with the MPL-2.0 Exhibit A notice, in the file's comment syntax:

  ```
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
  ```

  `tools/lint.py` enforces this for source files outside `third_party/` and `patches/`.
- Contributions are accepted under the **Developer Certificate of Origin** (`Signed-off-by`), not a CLA.
- Patches to Chromium files are modifications of BSD-3-Clause code. The patched files remain under Chromium's license. Our changes to them are contributed under the same terms, so that they could be upstreamed.

## Upstream Chromium

Chromium is BSD-3-Clause. It bundles hundreds of third-party libraries under their own licenses, mostly permissive, some LGPL (e.g. FFmpeg in its LGPL configuration).

Every release must:
- ship the generated credits page (`chrome://credits`), produced by Chromium's `tools/licenses` from each library's `README.chromium` metadata, including our additions;
- keep Chromium's copyright notices and license texts in the installer.

## Dependency policy

| Category | Licenses | Rule |
|---|---|---|
| Allowed | MIT, BSD-2-Clause, BSD-3-Clause, Apache-2.0, MPL-2.0, ISC, Zlib, Unicode-3.0, CC0-1.0 | Allowed after the normal dependency review |
| Review required | LGPL-2.1/3.0 (only with Chromium-style dynamic or replaceable linking), CC BY / CC BY-SA (data only, never code) | Needs a maintainer sign-off recorded in the PR, with the obligations written down |
| Prohibited in shipped code | GPL-2.0/3.0, AGPL, SSPL, BUSL and other source-available licenses, any NonCommercial license, code with no license | Not accepted |

**Adding a dependency**
1. Open an issue stating why the dependency is needed, what it replaces, and its license.
2. Vendor it under `third_party/` (C/C++) or `third_party/rust/` (crates), pinned to an exact version.
3. Add `README.chromium` metadata: `Name`, `URL`, `Version`, `License`, `License File`, `Security Critical`, `Shipped`. This feeds the credits page and the SBOM.
4. Two maintainers approve ([CONTRIBUTING.md](../CONTRIBUTING.md#review-requirements)).

## Code from other browsers and blockers

| Source | License | What we may do |
|---|---|---|
| Chromium | BSD-3-Clause | Everything; keep notices |
| brave-core | MPL-2.0 | Copy individual files, keeping their MPL notices and Brave copyright lines; changes to those files stay MPL-2.0. Never copy Brave trademarks, names, or service endpoints. |
| adblock-rust | MPL-2.0 | Use as a vendored dependency |
| ungoogled-chromium | BSD-3-Clause | Cherry-pick patches with attribution in the patch message |
| Tor Browser, Mullvad Browser | MPL-2.0 (Gecko-based) | Design reference for fingerprinting standardization; their code doesn't apply to Chromium |
| Cromite, Bromite | GPL-3.0 | **Don't copy.** Read for ideas only; implement independently. |
| uBlock Origin (code and scriptlet resources) | GPL-3.0 | **Don't copy.** See below. |

## Filter lists and scriptlets

- **Filter lists are data, not code.** They are distributed as separate, signed components, not compiled into the binary or into source files. Each list's attribution and license are shown in the browser's credits and on the list settings page.
  - **EasyList and EasyPrivacy** are dual-licensed GPL-3.0 / CC BY-SA 3.0. We distribute them unmodified as separate data, with attribution. Any list we derive from them is published under the same terms.
  - **uBlock Origin's filter lists** (GPL-3.0) may be offered as optional, user-enabled lists, distributed separately. This needs legal confirmation before release.
  - **Other lists**, such as regional and annoyance lists, are reviewed one by one. Some community lists restrict commercial redistribution.
- **Scriptlets are code.**
  - uBlock Origin's scriptlet and redirect resources are GPL-3.0, so we write our own under MPL-2.0.
  - Lists that reference uBlock-only scriptlets degrade gracefully: the rule is skipped.
  - Aggregating uBlock's resources as a separate optional component needs legal review first.

## Assets

- Icons are drawn for this project or taken from permissively licensed sets (Apache-2.0 Material Symbols, which Chromium already ships).
- The logo was drawn for this project by its author, who holds its rights; `branding/logo/` holds the source.
- Fonts are system fonts, or fonts under the SIL Open Font License.
- The Code of Conduct is adapted from the Contributor Covenant 2.1 (CC BY 4.0), with attribution kept in the file.

## Release gates

These must be settled, and recorded in an ADR, before the first public build.

| Gate | Question | Consequence if unresolved |
|---|---|---|
| Name and trademarks | Clearance for the final product name | No public build; see [Trademarks](#trademarks) |
| Media codecs | Patent licensing for H.264, AAC and HEVC decoding (`proprietary_codecs`); how much the Windows platform decoders cover | Build without proprietary codecs, so many videos won't play |
| Widevine DRM | Requires a license agreement with Google | Netflix, Spotify, Disney+ and similar don't work |
| Safe Browsing | Google's Safe Browsing APIs are for non-commercial use; commercial use requires Web Risk or an agreement | Reduced protection from malware and phishing sites |
| Google API keys | Chromium forks may not ship Google's keys | Features that depend on Google APIs stay disabled (already our default) |
| Mirroring Google components | Terms for redistributing CRLSet, certificate transparency data and file-type policies from our servers | Clients must fetch them from Google directly, which is a disclosed exception on the egress allowlist |
| Chrome Web Store | Using the store from a third-party browser; update requests go to Google | Disclose; consider a privacy-preserving update proxy later |
| Default search engine | DuckDuckGo is the default without any agreement; whether to take a revenue agreement with a search provider, and on what privacy terms | DuckDuckGo stays the default, with no search revenue |

## Trademarks

- **The product is "Shade"** (decided 2026-10-09); "Ghost" stays as the internal codename, which users never see.
- **Not cleared yet.** A quick search on 2026-10-09 found no browser named Shade, but found a US trademark application for "SHADES" (Shades Media, Inc., serial 97720908, filed 2022-12-16) covering downloadable software for accessing the internet, and nearby names: a tab-masking browser extension called Shade, ShadeYouVPN, NetShade. A trademark attorney's search (USPTO and EUIPO) decides the release gate.
- **The domain, browseshade.com, isn't bought yet.** Identifiers such as `com.browseshade.browser` work without it, but would have to change if someone else registered it. Buy it before the first public build.
- **Ghost Browser** (ghostbrowser.com) is an existing Chromium-based browser that offers color-coded per-tab "Identities" with per-identity proxies. That's the same name, the same feature term and an overlapping audience. Ghostery also operates in the browser-privacy space.
- We treat "Ghost" as unavailable for the product, and will choose a user-facing term for identities that doesn't collide.

The name must be final before the first build that creates user data directories, application IDs, ProgIDs or update endpoints, because changing those afterwards requires migrating every install.

We may say that the browser is "based on Chromium". We don't use Google's or Chrome's trademarks or logos, or other browsers' names, in our branding.
