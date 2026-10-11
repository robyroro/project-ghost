# Protection settings: design

- Status: design approved 2026-10-11 (each choice by the user, from mockups and questions; the rest decided by Claude with the user's approval, each decision with its reason below)
- Phase 3, sub-project 3D-4 ([roadmap](../../roadmap.md#phase-3-network-protections)), the last of 3D; it sets what 3D-1's policy ([design](2026-10-10-protection-levels-design.md)) takes as given and links from 3D-3's panel ([design](2026-10-10-protections-panel-design.md))

## Goal

A section of `chrome://settings` where the user chooses the default protection level for the sites they haven't set, turns Global Privacy Control and campaign-parameter stripping on or off, and sees and changes the sites that have their own level.

## Decisions

Decided by the user, 2026-10-11:

- **The settings are a section of `chrome://settings`**, "Shade protections", in the left menu above "Privacy and security", at `chrome://settings/protections`. Rejected: a page of our own (`chrome://protections`; settings in two places, and Settings' search doesn't find it); the panel (it would mix one site's choice with every site's, and the site list doesn't fit).
- **The section is modern, the rest of Settings stays Chromium's**: the levels are large vertical cards, in the spirit of Firefox's tracking protection; the one chosen opens and lists what it does. Rejected: three horizontal tiles (denser, but the level is explained in one line). Restyling all of Settings was not proposed: every milestone move would rebase it.
- **The default level can be Off, Standard or Strict.** Off carries a warning in its card. Rejected: Standard or Strict only, with Off per site only.
- **Incognito stays Strict** whatever the default level; the setting is for regular windows, and says so. Sites' own levels still reach Incognito (3D-1). Rejected: a second default for Incognito; one default for both modes.
- **GPC's switch applies at restart**, with Chromium's Relaunch button, as "Use graphics acceleration when available" does: a Local State pref read when the feature list is created. Rejected: live and per profile through `RendererPreferences`, Do Not Track's path (patches in about ten content and Blink files, rebased at each milestone).
- **The section's code lives in `//ghost`**, built as its own TypeScript library and mapped into the Settings bundle; Chromium's files get only glue. Rejected: the files created inside `chrome/browser/resources/settings` by a patch (all of the UI in patches); the page in an iframe (Settings' search and focus break).

Decided by Claude, approved with the design:

- **The default level is a profile pref**, `ghost.privacy_policy.default_level`: `"off"`, `"standard"` or `"strict"`, default `"standard"`, not synced. An unknown value reads as Standard.
- **GPC is a Local State pref**, `ghost.gpc.enabled`, default `true`: the feature list is created before any profile, and the feature is process-wide. When it is `false`, Shade doesn't force `kGlobalPrivacyControlForce` and upstream's default (off) applies. `--disable-features` and `chrome://flags` still have the last word, as now.
- **The site list shows the regular profile's choices.** Incognito's live in memory and end with the session. A row's level can be changed or the row removed; there is no "Add site": a site is set from the panel, on the site.
- **Changing the default leaves the list alone.** A site set to Strict stays Strict when the default becomes Strict, and keeps it when the default changes back. The panel already clears a site's entry when the default is chosen there, so the list doesn't fill with duplicates.
- **The panel follows the default**: its "default" label moves to the level chosen in Settings; with Off as the default the shield is grey everywhere. The panel gains a "Protection settings" link to the section.
- **Reads go through `settingsPrivate`, list writes through our handler.** The cards and switches bind to the prefs as Settings' own controls do; the list reads the dictionary the same way, so a change made in the panel shows without a reload. Writes to the list go through `setSiteLevel` and `clearSiteLevel`, so the level and the registrable domain are validated in C++ (`site_levels`), not in TypeScript.

## What the user sees

The section, top to bottom:

- **Header:** "Shade protections"; one sentence: choose how much Shade blocks on the sites you visit, and change it for any site from the shield in the toolbar.
- **Default level:** three cards, a radio group reachable by keyboard.
  - *Off:* Nothing is blocked. Cookie, HTTPS and GPC protections still apply. Warning: trackers and ads load on every site you haven't set.
  - *Standard* ("Recommended"): Blocks third-party trackers and ads. Removes click identifiers from links.
  - *Strict:* Also blocks the site's own trackers. Removes campaign parameters. Sends no referrer to other sites. Some sites may break.
  - The open card lists the sentences of 3D-3's panel, so the two never disagree. Under the cards: "Incognito windows always start at Strict."
- **More protections:**
  - *Send Global Privacy Control:* tells sites not to sell or share your data; Chromium's Relaunch button shows while the value differs from the one the browser started with.
  - *Remove campaign parameters:* `utm_source`, `utm_medium` and the rest; "Always removed in Incognito and at Strict."
- **Sites with their own level:** favicon, registrable domain, a level menu, a remove button. Empty: "Sites you set from the shield in the toolbar appear here."

Rounded corners and more air than Chromium's rows; colours only from `--cr-*` and `--color-sys-*` variables, so light, dark and high contrast follow. Strings are English only.

## Components

### `//ghost/browser/privacy_policy/`

| Unit | Change |
|---|---|
| `site_levels.{h,cc}` | Registers `kDefaultLevelPref`. `ModeDefault(context)`: Strict off the record, otherwise the pref's level (Standard when unparsable). |

### `//ghost/browser/features/`

| Unit | Change |
|---|---|
| `feature_overrides.{h,cc}` | `AppendFeatureOverrides(const PrefService& local_state, overrides)`: forces GPC only when `ghost.gpc.enabled` is true. |
| `local_state_prefs.{h,cc}` (new) | `kGpcEnabledPref`; `RegisterLocalStatePrefs(registry)`. |

### The section

| Unit | What it does |
|---|---|
| `//ghost/browser/resources/settings/` | A TypeScript and Lit library with its own `build_ts`: `shade-protections-page`, `shade-level-card`, `shade-site-list`, and a browser proxy for the handler. Uses Chromium's `cr-radio-group`, `settings-toggle-button`, `cr-icon-button`, `cr-action-menu`, and `chrome://favicon2` for the favicons (local cache, no network). |
| `//ghost/browser/ui/webui/settings/protections_handler` | A `settings::SettingsPageUIHandler`: `setSiteLevel(site, level)` and `clearSiteLevel(site)` call `site_levels`; an unknown level or a site without a host is refused. |
| `//ghost/browser/ui/webui/settings/settings_strings` | `AddSettingsStrings(WebUIDataSource*)`: the section's strings, English only, in C++ like the panel's. |
| `//ghost/browser/ui/webui/settings/settings_hooks` | `AddSettingsHandlers(WebUI*, Profile*)`, the one call Settings makes into Ghost for handlers. |
| `//ghost/browser/resources/protections/` (3D-3) | The panel's "default" label follows the default level; a "Protection settings" link. |

### Patches (numbered in commit order, after 0037)

| Patch | Files | What it adds |
|---|---|---|
| Settings bundle | `chrome/browser/resources/settings/BUILD.gn` | Our library in `ts_deps` and a `ts_path_mappings` entry for `/shade/*`, the way `settings_shared` is mapped as `/shared/settings/*`; its files in the Settings pak. |
| Settings section | `route.ts`, `settings_menu.html`, `settings_main.html` | The route `/protections`, the menu entry with the shield icon, the view that renders `shade-protections-page`. |
| Settings prefs | `chrome/browser/extensions/api/settings_private/prefs_util.cc` | The four prefs on `settingsPrivate`'s allow-list: default level (string), campaign parameters (boolean), site levels (dictionary), GPC (boolean, Local State). |
| Settings hooks | `settings_ui.cc`, `settings_localized_strings_provider.cc` | One line each: `ghost::AddSettingsHandlers`, `ghost::AddSettingsStrings`. |
| Local State | `chrome/browser/prefs/browser_prefs.cc`, `chrome_feature_list_creator.cc` | `ghost::RegisterLocalStatePrefs`; `AppendFeatureOverrides` receives `*local_state_`. Patch 0003 stays as it is: the new patch changes its line. |

## For the spike

1. That a section served from `//ghost` shows in Settings, **in a build with `optimize_webui=true`** too: the bundler runs only in official builds, and a wrong path would show only in `out/release`.
2. That Settings' search finds the section's text.
3. That `settingsPrivate` serves a dictionary pref to the page and updates it on change.
4. That Local State is loaded and our pref registered when `ChromeFeatureListCreator` calls `AppendFeatureOverrides`.

## Testing

**Unit** (`ghost_unittests`):
- `ModeDefault`: the pref's level in a regular profile, for each of the three; Strict off the record whatever the pref says; Standard for an unknown value.
- `GetLevel`: a site with its own level keeps it after the default changes.
- The panel's state: "default" is the default level; choosing it in the panel clears the site's entry.
- `AppendFeatureOverrides`: GPC is forced with `ghost.gpc.enabled` true and not with false.
- The handler: `setSiteLevel` refuses an unknown level and a site without a host; it keys by registrable domain (`www.a.test` → `a.test`); `clearSiteLevel` removes the entry.

**Browser** (`ghost_browsertests`):
- `chrome://settings/protections` loads with the section, the three cards, the two switches and the list.
- Clicking Strict (script in the page) writes the pref, and a new page blocks a same-site tracker.
- Off as the default: the tracker loads and the toolbar button is grey.
- Incognito stays Strict with Off as the default.
- The list shows a site set from the panel without reloading Settings; its remove button clears the entry and the site returns to the default.
- The campaign switch strips `utm_source` at Standard.
- GPC off: a `PRE_` test sets the Local State pref to false; after the restart no request carries `Sec-GPC` and `navigator.globalPrivacyControl` is false.
- Searching Settings for "tracker" finds the section.
- The panel's link opens `chrome://settings/protections`.

**The official build:** the section loads in `out/release` (bundled by `optimize_webui`), checked before the mutation checks.

**Mutation checks**, each must fail: M1 `ModeDefault` ignores `default_level`; M2 Incognito follows `default_level`; M3 GPC is forced when the pref is false; M4 the handler keys by the full host instead of the registrable domain; M5 the site list isn't on `settingsPrivate`'s allow-list.

**The egress audit** stays at no unexpected host: the section makes no network request.

**The look**, checked by the user in a dev build: light and dark, 100% and 150% scaling.

## Documentation

privacy-model.md (the settings; GPC can be turned off), architecture.md (the Settings library and its patches), testing.md, roadmap.md (3D-4 done; 3D closed), progress notes.

## Done when

- [ ] The tests pass and the five mutation checks fail as required.
- [ ] The section loads in the official build.
- [ ] The egress audit finds no unexpected host.
- [ ] The user approves the look.
- [ ] The documentation is updated; everything is committed (pushed when the user approves).

## Out of scope

- GPC live or per profile.
- Adding a site to the list by hand; importing or exporting the list.
- Translations.
- Restyling the rest of Settings.
- Per-identity protections (Phase 6).
