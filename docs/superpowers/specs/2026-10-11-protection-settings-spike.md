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

## Task 5: the handler for the site list, 2026-10-11

- `ProtectionsHandler` accepts only a bare host as the site. A path, a space, a non-string, an empty string or an unknown level is refused. It stores the site under its registrable domain through `site_levels`. 4 unit tests.
- **At the pin, base's value types are `base::ListValue` and `base::DictValue`**; `base::Value::List` no longer exists. `content::TestWebUI::AddMessageHandler` registers the handler's messages itself.
- `//ghost/browser/ui/settings` depends on `//chrome/browser/ui/webui/settings:settings` (the `SettingsPageUIHandler` base), which doesn't depend on `:impl`: no cycle, and `gn check` passes.

## Task 6: the strings, 2026-10-11

- The levels' names and sentences moved from the panel's strings file into `//ghost/browser/ui/protections:level_strings`, which depends only on the policy component. The panel and the Settings section both read them. Settings can't depend on the panel's target, which links the toolbar.
- `shadeGpcEnabledAtStartup` is `blink::IsGlobalPrivacyControlEnabled()` when the page loads: GPC as this process runs it.

## Task 7: the section, 2026-10-11

- **No handler: the site list writes through `settingsPrivate` too.** Chromium's WebUI lint (`no-restricted-properties`) refuses `chrome.send` in any WebUI not on a frozen list of legacy users (`build_webui.gni`, "DO NOT add new entries"). The other routes were a Mojo interface, at the cost of two more Settings patches (the interface's binder and `SettingsUI`), or joining that list. Instead, the list writes the dictionary through `PrefService` (`setPrefDictEntry`, and the dictionary without the key to remove). It only changes or removes keys the panel wrote, which are registrable domains already, and `settingsPrivate` checks the pref's type. Task 5's handler and its 4 tests are removed, and mutation M4 targets the list instead. The spec records the change.
- **The level cards are the `cr-radio-button`s themselves.** The button's slot holds the chip, the summary, the details and the warning. The plan's separate `shade-level-card` element would have added nothing.
- **The menu's shield is `settings20:shield`.** The `settings` iconset has none, so the first build showed no icon. This was fixed in patch 0038.
- WebUI lint names: a `selected-changed` listener must be called `on<Context>SelectedChanged_`.
- **Checked by hand** in the dev build (DevTools on a fresh profile):
  - Standard is checked by default; clicking Strict writes `default_level` and opens Strict's card;
  - a site written to the pref shows in the list without a reload; the list's menu changes its level; ✕ removes it, and the empty text shows;
  - turning GPC off shows Relaunch, and turning it back on hides it;
  - the campaign switch writes its pref;
  - a search for "tracker" matches.
  - The look in dark mode matches the approved mockup.
- Totals: 98 unit tests (the 4 handler tests removed) and 83 browser tests pass, with no retry.

## Task 8: the panel's link, 2026-10-11

- `PageHandler.OpenSettings()` opens `chrome://settings/protections` in a new foreground tab, from the bubble's active tab (`WebContents::OpenURL`, content API only), then closes the bubble (`ProtectionsUI::CloseUI`). The panel has a "Protection settings" link at its foot.

## Task 9: browser tests, 2026-10-11

- `ProtectionSettingsBrowserTest` has 7 tests: the prefs Settings sees, the section's controls, Strict writes the default, the list follows the panel then changes and removes a site, the campaign switch, GPC's Relaunch (it appears, then goes when GPC is back on), and search. `ProtectionsPanelBrowserTest` gains 2: the link opens the section in a new tab, and an Off default turns the button off.
- The helpers find the section through Settings' nested shadow roots (`findDeep`) and poll (`waitFor`). The browser-side checks use `base::test::RunUntil`.
- **The suites run on a hidden Windows desktop from now on.** The user's screen showed the tests' browser windows. A launcher creates a desktop (`CreateDesktop`) and starts the test there (`STARTUPINFO.lpDesktop`); child processes inherit it. Views and WebUI tests pass there unchanged.
- Totals: 98 unit and 91 browser tests pass, with no retry.

## Task 11: mutation checks, audit, docs, 2026-10-11

- **Mutation checks**, run with each mutant's build checked first and the tests on the hidden desktop. Each fails as required:
  - **M1:** `ModeDefault` ignores the default-level pref. Caught by `SiteLevelsTest.TheRegularProfileUsesTheDefaultLevel`, `PanelStateTest.TheDefaultIsTheChosenDefaultLevel`, `ProtectionLevelsBrowserTest.AStrictDefaultBlocksTheSitesOwnTrackers` and `.AnOffDefaultLoadsTrackers`, and `ProtectionsPanelBrowserTest.AnOffDefaultTurnsTheButtonOff`.
  - **M2:** Incognito reads the pref. Caught by `SiteLevelsTest.IncognitoIsStrictWhateverTheDefault` (and three more) and `ProtectionLevelsBrowserTest.IncognitoIgnoresAnOffDefault` and `.IncognitoIsStrictByDefault`.
  - **M3:** GPC forced whatever its pref says. Caught by `FeatureOverridesTest.GpcTurnedOffIsLeftToUpstream` and `NetworkDefaultsBrowserTest.GpcCanBeTurnedOff`.
  - **M4:** the list's remove button writes the dictionary back unchanged. Caught by `ProtectionSettingsBrowserTest.TheListFollowsThePanel`.
  - **M5:** patch 0040 without the site-levels line. Caught by `ProtectionSettingsBrowserTest.SettingsSeesShadesPrefs` and `.TheListFollowsThePanel`.
- **The egress audit** (dev build, on the hidden desktop) finds 0 unexpected hosts. Not counted, as always: one route probe and the `wpad` lookups. The section's favicons come from the local cache.
- **Docs:** privacy-model.md (the settings; GPC can be turned off), architecture.md (the Settings library and patches 0038–0040), testing.md (the new tests; the hidden desktop), roadmap.md.
- **The look:** the user checked the section in the dev build and approved it (2026-10-11).
- **The official build check** was skipped by the user (2026-10-11). Dev builds already run the bundled path. A full `out\release` build takes hours and belongs to the release workflow; the next release run builds the section anyway.
