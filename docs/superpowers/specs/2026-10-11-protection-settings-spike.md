# Protection settings: progress notes

[Design](2026-10-11-protection-settings-design.md), [plan](../plans/2026-10-11-protection-settings.md).

## Task 1: an empty section end to end (spike, patch 0038), 2026-10-11

What the wiring took, beyond the plan:

- **Dev builds bundle Settings too.** `optimize_webui` defaults to `!is_debug` (`ui/webui/webui_features.gni`), and `out\vanilla` isn't a debug build. So the dev build already runs the bundled path the plan feared would show only in `out\release`. The section's code is in `chrome://settings/shared.rollup.js`, with no request for `/shade/`. The plan's bundle check in `out\release` (Step 7) was therefore unnecessary and wasn't run.
- **The `!optimize_webui` branches run only in debug builds.** These are `chrome_paks.gni` packing `shade_settings_resources.pak` and `SetUpSettingsPage` adding the `/shade/` resource paths. They copy upstream's handling of `settings_shared` line for line; no Shade build exercises them.
- **`findAndRemoveHighlights` asserts exactly one wrapper.** It is meant for Settings' own one-node bubbles. `searchTree` keeps the wrappers it made per shadow root and removes them with `removeHighlights` at the next search.
- **WebUI's ESLint runs in the build.** It rejects a non-null assertion on `CrLitElement.shadowRoot`, which is typed non-null already.
- **No visibility list blocked the library.** `settings_shared:build_ts` and `cr_elements:build_ts` are public. Lit's list already allows `//ghost/browser/resources/*` (patch 0036).
- **Local State is ready for `AppendFeatureOverrides`.** `ChromeFeatureListCreator::CreateFeatureList` calls `CreatePrefService()` (line 65, which registers every Local State pref) before `SetUpFieldTrials()` (line 69).
- **`settingsPrivate` returns `undefined` for a pref not on its allow-list.** Task 4's test sees `"null"` from `JSON.stringify` of nothing until patch 0039.
- **Proof** (dev build, fresh profile, through DevTools):
  - the menu shows "Shade protections" between "Autofill and passwords" and "Privacy and security";
  - `chrome://settings/protections` renders the section;
  - `?search=shade` keeps the section visible and highlights the title.
- **Builds:** 1m35s to the first error (ESLint), then 2m34s; each later rebuild under 1m30s. A running dev browser holds `resources.pak` and fails the repack: close it before building.

## Task 2: the default level, 2026-10-11

- `ghost.privacy_policy.default_level` as planned: `ModeDefault` reads it in a regular profile; Incognito stays Strict.
- The panel needed no change: its "default" label already comes from `ModeDefault`.
- Tests: 5 unit tests (4 in `SiteLevels`, 1 in `PanelState`) and 3 browser tests in `ProtectionLevels`.
- Totals: 96 unit and 80 browser tests pass, with no retry.

## Task 3: GPC's switch (patch 0039), 2026-10-11

- `ghost.gpc.enabled` in Local State. `AppendFeatureOverrides` forces `kGlobalPrivacyControlForce` only while it is true.
- Patch 0039 registers the pref and passes `*local_state_` to `AppendFeatureOverrides`. Task 3 came before the allow-list (Task 4), so it took 0039, and the allow-list is 0040. The plan and spec are renumbered.
- **Off, upstream exposes no `navigator.globalPrivacyControl`.** The property is `undefined`, not `false`, as upstream's own tests expect (`content/browser/global_privacy_control_browsertest.cc` compares with `=== true`). `GpcCanBeTurnedOff` does the same.
- The test is a `PRE_` pair: `PRE_GpcCanBeTurnedOff` writes the pref, and the restart applies it. The existing GPC tests still pass with the default (on).
- Totals: 98 unit and 82 browser tests pass, with no retry.

## Task 4: the prefs on settingsPrivate (patch 0040), 2026-10-11

- The four prefs are on `PrefsUtil::GetAllowlistedKeys()`. `//chrome/browser/extensions` depends on `//ghost/browser/features`, `privacy_policy` and `query_filter:prefs`, with no cycle: `gn check` passes on every touched target.
- `SettingsSeesShadesPrefs` failed with every value `"null"` before the patch and passes after it. It reads the Local State pref too: `settingsPrivate` finds Local State prefs by itself (`PrefsUtil::FindServiceForPref`).
- Totals: 98 unit and 83 browser tests pass, with no retry.
