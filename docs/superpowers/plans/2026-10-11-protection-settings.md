# Protection Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A "Shade protections" section of `chrome://settings` sets the default level, Global Privacy Control and campaign-parameter stripping, and lists the sites with their own level ([spec](../specs/2026-10-11-protection-settings-design.md)).

**Architecture:** The section is a TypeScript and Lit library in `//ghost/browser/resources/settings`, served by the Settings host under `/shade/` and bundled into `settings.js` in official builds. This is the way Chromium maps its own `settings_shared` library (`/shared/settings/`). It reads and writes prefs through `settings_shared`'s `PrefService` (`settingsPrivate`), and writes the site list through `ProtectionsHandler`, a `SettingsPageUIHandler`. Three patches carry the glue:
- 0038: the section;
- 0039: GPC's Local State pref and the feature list (Task 3 came first, so it took 0039; renumbered while executing);
- 0040: the prefs on `settingsPrivate`'s allow-list.

**Tech Stack:** C++, TypeScript, Lit, `build_webui`, `settingsPrivate`, Chromium 152.0.7977.158 Settings; `ChromeRenderViewHostTestHarness`, `InProcessBrowserTest`.

**Settled while planning** (each confirmed in the source at the pin; the spec is amended to match):
- **No cycle with Settings.** The library imports nothing from `chrome/browser/resources/settings`:
  - it depends on `//chrome/browser/resources/settings_shared:build_ts` (public), Lit, `cr_elements` and `//resources/js`, while Settings depends on it;
  - it reads prefs with `settings_shared`'s `prefs2/PrefService` and `PrefServiceObserverMixinLit`, which are Lit-ready and handle dictionaries;
  - it relaunches through `settings_shared`'s `LifetimeBrowserProxyImpl`;
  - its `searchContents` uses `//resources/js/search_highlight_utils.js` directly, since Settings' search manager lives in Settings.

  `SettingsPlugin` and `SearchResult` are typed structurally in our code: `{canceled, matchCount, wasClearSearch}`.
- **The bundling copies `settings_shared` exactly.** In `chrome/browser/resources/settings/BUILD.gn` our library goes into three places:
  - `ts_deps`;
  - `ts_path_mappings` (`/shade/*`);
  - `optimize_webui_external_paths` (`/shade|…`).

  `chrome/chrome_paks.gni` adds our pak only when `!optimize_webui`, as it does for `settings_shared_resources.pak`. `resource_ids.spec` needs an entry for our grd.
- **One hook in `settings_ui.cc`, none in the strings provider.** After `AddLocalizedStrings(...)`, `ghost::settings::SetUpSettingsPage(web_ui, html_source)` adds:
  - our strings;
  - the `/shade/` resource paths (only when `!BUILDFLAG(OPTIMIZE_WEBUI)`, from `chrome/common/buildflags.h`, as for `kSettingsSharedResources`);
  - the handler.

  The spec listed `settings_localized_strings_provider.cc`; it isn't touched.
- **The section's route needs `router.ts` too.** `SettingsRoutes` is an interface listing every route, so it needs `PROTECTIONS: Route`. `settings_main.ts` imports our element (`import '/shade/protections_page.js';`).
- **The menu icon is `settings20:shield`**, already in Settings' icon set.
- **The relaunch indicator follows Settings' hardware acceleration toggle.** A load-time boolean `shadeGpcEnabledAtStartup` is `blink::IsGlobalPrivacyControlEnabled()` at page load. The Relaunch button shows while the pref differs from it.
- **The panel opens the section with `WebContents::OpenURL`** on the tab, in a new foreground tab. This is content API, so `//ghost/browser/ui/protections` gains no dependency on `chrome_pages`. The panel's "default" label already follows `ModeDefault` through `GetPanelState`, so it needs no change.
- **Patch numbers follow commit order:** 0038 is the spike's section patch (bundle, route, menu, view, hook), 0040 the allow-list, 0039 Local State.

---

### Task 1: Spike — an empty section end to end (patch 0038)

The bundling is the uncertain part. Prove it with a section that only says "Shade protections", before any logic.

**Files:**
- Create: `browser/resources/settings/BUILD.gn`, `protections_page.ts`, `protections_page.html.ts`, `search.ts`
- Create: `browser/ui/settings/BUILD.gn`, `settings_page.{h,cc}` (only `SetUpSettingsPage` with the resource paths and one string, `shadeProtectionsTitle`)
- Create: patch 0038 in `chromium/src`, `git commit -s` with `Why:` and `Upstream:` trailers
- Create: `docs/superpowers/specs/2026-10-11-protection-settings-spike.md` (progress notes)

- [x] **Step 1:** Write the library's target, on `settings_shared`'s model. In `browser/resources/settings/BUILD.gn`:

```gn
import("//ui/webui/resources/tools/build_webui.gni")

# Shade's section of chrome://settings, served by the Settings host under
# /shade/ (patches/0038) and bundled into settings.js when optimize_webui.
build_webui("build") {
  grd_prefix = "shade_settings"
  grd_resource_path_prefix = "shade"
  grit_output_dir = "$root_gen_dir/ghost"

  ts_files = [
    "protections_page.html.ts",
    "protections_page.ts",
    "search.ts",
  ]

  ts_composite = true
  ts_definitions = [
    "//tools/typescript/definitions/chrome_event.d.ts",
    "//tools/typescript/definitions/chrome_send.d.ts",
    "//tools/typescript/definitions/settings_private.d.ts",
  ]
  ts_deps = [
    "//chrome/browser/resources/settings_shared:build_ts",
    "//third_party/lit/v3_0:build_ts",
    "//ui/webui/resources/cr_elements:build_ts",
    "//ui/webui/resources/js:build_ts",
  ]
  ts_path_mappings =
      [ "/shared/settings/*|" + rebase_path(
            "$root_gen_dir/chrome/browser/resources/settings_shared/tsc/*",
            target_gen_dir) ]

  webui_context_type = "trusted"
}
```

- [x] **Step 2:** Write `search.ts`: one function used by the section and its tests.

```ts
import {findAndRemoveHighlights, highlight, stripDiacritics} from '//resources/js/search_highlight_utils.js';
import type {Range} from '//resources/js/search_highlight_utils.js';

// What chrome://settings' <settings-main> expects from a plugin's
// searchContents (settings/search_settings.ts, typed here to avoid importing
// Settings, which depends on this library).
export interface SearchResult {
  canceled: boolean;
  matchCount: number;
  wasClearSearch: boolean;
}

function escape(text: string): string {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// Highlights |query| in the text of |root| (visible text only: closed
// cards render no list) and counts the matches.
export function searchTree(root: ShadowRoot, query: string): SearchResult {
  findAndRemoveHighlights(root);
  if (query === '') {
    return {canceled: false, matchCount: 0, wasClearSearch: true};
  }
  const pattern = new RegExp(escape(stripDiacritics(query)), 'gi');
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  const nodes: Text[] = [];
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    if (node.parentElement?.closest('style, [no-search]') === null) {
      nodes.push(node as Text);
    }
  }
  let matchCount = 0;
  for (const node of nodes) {
    const ranges: Range[] = [];
    for (const match of stripDiacritics(node.data).matchAll(pattern)) {
      ranges.push({start: match.index, length: match[0].length});
    }
    if (ranges.length > 0) {
      highlight(node, ranges);
      matchCount += ranges.length;
    }
  }
  return {canceled: false, matchCount, wasClearSearch: false};
}
```

- [x] **Step 3:** Write `protections_page.ts`: a `CrLitElement` named `shade-protections-page`, which renders `<h1>${loadTimeData.getString('shadeProtectionsTitle')}</h1>` and implements `async searchContents(query: string): Promise<SearchResult> { await this.updateComplete; return searchTree(this.shadowRoot!, query); }`. Add a `declare global { interface HTMLElementTagNameMap { 'shade-protections-page': ... } }`.
- [x] **Step 4:** Write `browser/ui/settings/settings_page.{h,cc}`:

```cpp
namespace ghost::settings {

// Called by chrome://settings' SettingsUI (patches/0038) once its data
// source has upstream's strings: adds Shade's section's strings, its
// /shade/ resources (bundled into settings.js when optimize_webui), and its
// message handler.
void SetUpSettingsPage(content::WebUI* web_ui, content::WebUIDataSource* source);

}  // namespace ghost::settings
```

  In the `.cc`, under `#if !BUILDFLAG(OPTIMIZE_WEBUI)`, call `source->AddResourcePaths(kShadeSettingsResources)`, with includes `ghost/grit/shade_settings_resources.h` and `ghost/grit/shade_settings_resources_map.h`, and add `shadeProtectionsTitle` = "Shade protections".

  Its `BUILD.gn` is `source_set("settings")`, with deps `//base`, `//chrome/common:buildflags`, `//content/public/browser`, `//ghost/browser/resources/settings:resources`.
- [x] **Step 5:** Write patch 0038 in `chromium/src`:
  - `chrome/browser/resources/settings/BUILD.gn`:
    - `"//ghost/browser/resources/settings:build_ts"` in `ts_deps`;
    - in `ts_path_mappings`, `"/shade/*|" + rebase_path("$root_gen_dir/ghost/browser/resources/settings/tsc/*", target_gen_dir)`;
    - in `optimize_webui_external_paths`, `"/shade|" + rebase_path("$root_gen_dir/ghost/browser/resources/settings/tsc", root_build_dir)`.
  - `chrome/chrome_paks.gni`, inside the `if (!optimize_webui)` block beside `settings_shared_resources.pak`: `"$root_gen_dir/ghost/shade_settings_resources.pak"` and `"//ghost/browser/resources/settings:resources"`.
  - `tools/gritsettings/resource_ids.spec`, after Shade's protections panel entry:

```
  # Shade's section of chrome://settings.
  "<(SHARED_INTERMEDIATE_DIR)/ghost/browser/resources/settings/resources.grd": {
    "META": {"sizes": {"includes": [20]}},
    "includes": [10260],
  },
```

  - `route.ts`, after the `PRIVACY` block:

```ts
  // Shade: its protections section (patches/0038).
  r.PROTECTIONS = r.BASIC.createSection(
      '/protections', 'protections',
      loadTimeData.getString('shadeProtectionsTitle'));
```

  - `router.ts`: `PROTECTIONS: Route;` in `SettingsRoutes`.
  - `settings_menu/settings_menu.html`, before the `/privacy` entry:

```html
        <a role="menuitem" id="protections" href="/protections"
            class="cr-nav-menu-item">
          <cr-icon icon="settings20:shield"></cr-icon>
          $i18n{shadeProtectionsTitle}
          <cr-ripple></cr-ripple>
        </a>
```

  - `settings_main/settings_main.html`, before the privacy view:

```html
  <div slot="view" id="protections">
    <template is="dom-if" if="[[renderPlugin_(
        routes_.PROTECTIONS, lastRoute_, inSearchMode_)]]">
      <shade-protections-page></shade-protections-page>
    </template>
  </div>
```

  - `settings_main/settings_main.ts`: `import '/shade/protections_page.js';`.
  - `chrome/browser/ui/webui/settings/settings_ui.cc`, right after `AddLocalizedStrings(html_source, profile, web_ui->GetWebContents());`:

```cpp
  // Shade: its protections section (patches/0038).
  ghost::settings::SetUpSettingsPage(web_ui, html_source);
```

    Add `"//ghost/browser/ui/settings"` to the `deps` of the target in `chrome/browser/ui/webui/settings/BUILD.gn` that lists `settings_ui.cc`.
- [x] **Step 6:** Sync and build `chrome` in `out\vanilla`. Run it and open `chrome://settings/protections`. Expected:
  - the menu shows "Shade protections" with a shield, above "Privacy and security";
  - the section shows its title;
  - a search for "shade" highlights it and hides the sections without matches.
- [x] **Step 7 (not needed, see the progress notes: `out\vanilla` has `optimize_webui = true` and bundles the section already):** Bundling check without a full official build: `autoninja -C out\release chrome/browser/resources/settings:build_bundle`. If that name doesn't exist, find the `optimize_webui` target with `gn ls out\release //chrome/browser/resources/settings:*`. Expected:
  - it succeeds;
  - `out\release\gen\chrome\browser\resources\settings\bundled\settings.rollup.js` (or the path printed) contains `shade-protections-page`;
  - it has no import left for `/shade/`.

  Record the target name and the path. Don't build anything else in `out\release`, and don't touch `chrome/VERSION`.
- [x] **Step 8:** Check the dictionary pref: in DevTools on `chrome://settings`, `chrome.settingsPrivate.getPref('ghost.privacy_policy.site_levels', console.log)`. Expected: `undefined` (not allow-listed yet). Record what `settingsPrivate` does with a dictionary pref in Task 4.
- [x] **Step 9:** Check Local State timing: in `chrome_feature_list_creator.cc`, confirm `CreatePrefService()` (which registers all Local State prefs via `RegisterLocalState`) runs before `SetUpFieldTrials()`. Record the line numbers.
- [x] **Step 10:** Record the build time, every surprise, and the bundle check in the progress notes. Commit in webops: `settings: an empty Shade section end to end (patch 0038): spike`. Re-export the patches with `patches.py export`; `patches.py check` must pass.

### Task 2: The default level

**Files:**
- Modify: `browser/privacy_policy/site_levels.{h,cc}`
- Test: `browser/privacy_policy/site_levels_unittest.cc`

- [x] **Step 1: Write the failing tests** (in `site_levels_unittest.cc`):

```cpp
TEST_F(SiteLevelsTest, TheRegularProfileUsesTheDefaultLevel) {
  for (ProtectionLevel level :
       {ProtectionLevel::kOff, ProtectionLevel::kStandard, ProtectionLevel::kStrict}) {
    profile_.GetPrefs()->SetString(kDefaultLevelPref, ToPrefString(level));
    EXPECT_EQ(ModeDefault(&profile_), level);
    EXPECT_EQ(GetLevel(&profile_, GURL("https://a.test/")), level);
  }
}

TEST_F(SiteLevelsTest, IncognitoIsStrictWhateverTheDefault) {
  profile_.GetPrefs()->SetString(kDefaultLevelPref, "off");
  EXPECT_EQ(ModeDefault(Incognito()), ProtectionLevel::kStrict);
  EXPECT_EQ(GetLevel(Incognito(), GURL("https://a.test/")), ProtectionLevel::kStrict);
}

TEST_F(SiteLevelsTest, AnUnknownDefaultIsStandard) {
  profile_.GetPrefs()->SetString(kDefaultLevelPref, "strictest");
  EXPECT_EQ(ModeDefault(&profile_), ProtectionLevel::kStandard);
}

TEST_F(SiteLevelsTest, ASitesOwnLevelOutlivesADefaultChange) {
  SetLevel(&profile_, GURL("https://a.test/"), ProtectionLevel::kStrict);
  profile_.GetPrefs()->SetString(kDefaultLevelPref, "strict");
  profile_.GetPrefs()->SetString(kDefaultLevelPref, "standard");
  EXPECT_EQ(GetLevel(&profile_, GURL("https://a.test/")), ProtectionLevel::kStrict);
}
```

- [x] **Step 2: Run them to see them fail.** `autoninja -C out\vanilla ghost_unittests && out\vanilla\ghost_unittests --gtest_filter=SiteLevels*`. Expected: a compile failure (no `kDefaultLevelPref`).
- [x] **Step 3: Implement.** In `site_levels.h`:

```cpp
// The level of the sites the user hasn't set, in a regular profile: a level's
// pref string, Standard if unknown. Incognito ignores it (always Strict, the
// privacy model's Private mode). Not synced.
inline constexpr char kDefaultLevelPref[] = "ghost.privacy_policy.default_level";
```

  Update `ModeDefault`'s comment: "Strict off the record; otherwise the default level the user chose (kDefaultLevelPref)." In `site_levels.cc`:

```cpp
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(kSiteLevelsPref);
  registry->RegisterStringPref(kDefaultLevelPref, ToPrefString(ProtectionLevel::kStandard));
}

ProtectionLevel ModeDefault(content::BrowserContext* context) {
  if (context->IsOffTheRecord()) {
    return ProtectionLevel::kStrict;
  }
  return ParseProtectionLevel(Prefs(context)->GetString(kDefaultLevelPref))
      .value_or(ProtectionLevel::kStandard);
}
```

- [x] **Step 4: Run the tests.** Same command as Step 2, then all of `ghost_unittests`. Expected: pass. `PanelState*` still passes: its "default" label already reads `ModeDefault`.
- [x] **Step 5: Add one panel test** in `browser/protections/panel_state_unittest.cc`:

```cpp
TEST_F(PanelStateTest, TheDefaultIsTheChosenDefaultLevel) {
  profile()->GetPrefs()->SetString(privacy_policy::kDefaultLevelPref, "strict");
  NavigateAndCommit(GURL("https://news.test/"));
  EXPECT_EQ(GetPanelState(*web_contents()).mode_default, ProtectionLevel::kStrict);
  ChooseLevel(*web_contents(), ProtectionLevel::kStrict);
  EXPECT_FALSE(profile()->GetPrefs()->GetDict(privacy_policy::kSiteLevelsPref).contains("news.test"));
}
```

  Run `--gtest_filter=PanelState*`. Expected: pass.
- [x] **Step 6: Browser tests** in `browser/privacy_policy/protection_levels_browsertest.cc`:

```cpp
IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, AStrictDefaultBlocksTheSitesOwnTrackers) {
  GetProfile()->GetPrefs()->SetString(kDefaultLevelPref, "strict");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(Load("script", Url("a.test", "/own.js")), "error");
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, AnOffDefaultLoadsTrackers) {
  GetProfile()->GetPrefs()->SetString(kDefaultLevelPref, "off");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(Load("script", Url("tracker.test", "/t.js")), "loaded");
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, IncognitoIgnoresAnOffDefault) {
  GetProfile()->GetPrefs()->SetString(kDefaultLevelPref, "off");
  Browser* incognito = CreateIncognitoBrowser();
  const GURL page = embedded_https_test_server().GetURL("a.test", "/page");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, page));
  ASSERT_EQ(Load("script", page.Resolve("/fine.js"), incognito), "loaded");
  EXPECT_EQ(Load("script", page.Resolve("/own.js"), incognito), "error");
}
```

  Run `out\vanilla\ghost_browsertests --gtest_filter=ProtectionLevels*`. Expected: pass.
- [x] **Step 7: Commit.** `privacy_policy: the default level for a regular profile's sites`.

### Task 3: GPC's switch (patch 0039)

**Files:**
- Create: `browser/features/local_state_prefs.{h,cc}`
- Modify: `browser/features/feature_overrides.{h,cc}`, `browser/features/BUILD.gn` (deps `//components/prefs`)
- Create: patch 0039 (`chrome/browser/prefs/browser_prefs.cc`, `chrome/browser/metrics/chrome_feature_list_creator.cc`, and the `BUILD.gn` of `browser_prefs.cc`'s target)
- Test: `browser/features/feature_overrides_unittest.cc` (add to `ghost_unittests`), `test/network_defaults_browsertest.cc`

- [x] **Step 1: Write the failing unit tests** in `feature_overrides_unittest.cc`:

```cpp
namespace ghost {
namespace {

bool ForcesGpc(bool enabled) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  local_state.SetBoolean(kGpcEnabledPref, enabled);
  std::vector<base::FeatureList::FeatureOverrideInfo> overrides;
  AppendFeatureOverrides(local_state, &overrides);
  return std::ranges::any_of(overrides, [](const auto& entry) {
    return &entry.first.get() == &blink::features::kGlobalPrivacyControlForce &&
           entry.second == base::FeatureList::OVERRIDE_ENABLE_FEATURE;
  });
}

TEST(FeatureOverridesTest, GpcIsForcedByDefault) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  EXPECT_TRUE(local_state.GetBoolean(kGpcEnabledPref));
  EXPECT_TRUE(ForcesGpc(true));
}

TEST(FeatureOverridesTest, GpcTurnedOffIsLeftToUpstream) {
  EXPECT_FALSE(ForcesGpc(false));
}

}  // namespace
}  // namespace ghost
```

  Add `"browser/features/feature_overrides_unittest.cc"` to `ghost_unittests`' sources, and `"//ghost/browser/features"` and `"//components/prefs:test_support"` to its deps.
- [x] **Step 2: Run them to see them fail.** Expected: a compile failure.
- [x] **Step 3: Implement.** `local_state_prefs.h`:

```cpp
namespace ghost {

// Whether Shade sends Global Privacy Control (docs/privacy-model.md). Read
// once, when the feature list is created (AppendFeatureOverrides): a change
// applies at the next start.
inline constexpr char kGpcEnabledPref[] = "ghost.gpc.enabled";

// Shade's browser-wide prefs; chrome's RegisterLocalState() calls this
// (patches/0039).
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace ghost
```

  `local_state_prefs.cc` registers `kGpcEnabledPref` with default `true`. `feature_overrides.h` changes to `void AppendFeatureOverrides(const PrefService& local_state, std::vector<base::FeatureList::FeatureOverrideInfo>* overrides);` and its comment adds: "|local_state| is loaded with every Local State pref registered." In `feature_overrides.cc`, wrap the GPC override:

```cpp
  // Global Privacy Control ... (existing comment). The user can turn it off in
  // Shade's settings (kGpcEnabledPref); upstream's default (off) then applies.
  if (local_state.GetBoolean(kGpcEnabledPref)) {
    overrides->emplace_back(
        std::cref(blink::features::kGlobalPrivacyControlForce),
        base::FeatureList::OVERRIDE_ENABLE_FEATURE);
  }
```

  Add `local_state_prefs.{h,cc}` to `source_set("features")`, and `//components/prefs` to its `public_deps`.
- [x] **Step 4: Patch 0039** in `chromium/src`:
  - In `chrome_feature_list_creator.cc`, change 0003's line to `ghost::AppendFeatureOverrides(*local_state_, &extra_overrides);`. 0003 stays as it is: this patch changes the line after it.
  - In `browser_prefs.cc`, include `ghost/browser/features/local_state_prefs.h`. At the end of `RegisterLocalState(PrefRegistrySimple* registry)`:

```cpp
  // Shade's browser-wide prefs (patches/0039).
  ghost::RegisterLocalStatePrefs(registry);
```

    Add `"//ghost/browser/features"` to the deps of the target that lists `prefs/browser_prefs.cc` (find it with `gn refs out\vanilla //chrome/browser/prefs/browser_prefs.cc` or by searching `chrome/browser/BUILD.gn`).

  Commit with `-s`. `Why:` the user can turn GPC off; the switch is read where the feature list is made. `Upstream:` not upstreamable, product-specific.
- [x] **Step 5: Run the unit tests.** Expected: pass.
- [x] **Step 6: The browser test** in `test/network_defaults_browsertest.cc`, after the GPC tests:

```cpp
// GPC's switch is read at startup: PRE_ turns it off, the test runs after the
// restart.
IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, PRE_GpcCanBeTurnedOff) {
  g_browser_process->local_state()->SetBoolean(kGpcEnabledPref, false);
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, GpcCanBeTurnedOff) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(Header("a.test", "/page", "Sec-GPC"), "<none>");
  LoadImage(Url("b.test", "/img"));
  EXPECT_EQ(Header("b.test", "/img", "Sec-GPC"), "<none>");
  EXPECT_EQ(content::EvalJs(Tab(), "navigator.globalPrivacyControl"), false);
}
```

  If `Header` returns a different marker for a missing header in this file, use that marker (check the fixture). Run `out\vanilla\ghost_browsertests --gtest_filter=NetworkDefaults*`. Expected: all pass, with the two existing GPC tests still passing (default on).
- [x] **Step 7: Commit.** In webops: `features: GPC can be turned off, at the next start (patch 0039)`. Re-export the series; `patches.py check`.

### Task 4: The prefs on `settingsPrivate` (patch 0040)

**Files:**
- Create: patch 0040 (`chrome/browser/extensions/api/settings_private/prefs_util.cc` and its target's `BUILD.gn`)
- Test: `browser/ui/settings/protection_settings_browsertest.cc` (new; add to `ghost_browsertests`)

- [x] **Step 1: Write the failing browser test.**

```cpp
namespace ghost::settings {
namespace {

// Reads a pref the way chrome://settings does.
constexpr char kGetPref[] = R"((key => new Promise(done =>
    chrome.settingsPrivate.getPref(key, pref => done(pref ? JSON.stringify(pref.value) : 'null')))))";

class ProtectionSettingsBrowserTest : public InProcessBrowserTest {
 protected:
  content::WebContents* Tab() { return browser()->tab_strip_model()->GetActiveWebContents(); }

  content::EvalJsResult GetPref(std::string_view key) {
    return content::EvalJs(Tab(), content::JsReplace(base::StrCat({kGetPref, "($1)"}), key));
  }
};

IN_PROC_BROWSER_TEST_F(ProtectionSettingsBrowserTest, SettingsSeesShadesPrefs) {
  privacy_policy::SetLevel(browser()->profile(), GURL("https://news.test/"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/protections")));
  EXPECT_EQ(GetPref(privacy_policy::kDefaultLevelPref), "\"standard\"");
  EXPECT_EQ(GetPref(query_filter::kStripCampaignParametersPref), "false");
  EXPECT_EQ(GetPref(kGpcEnabledPref), "true");
  EXPECT_EQ(GetPref(privacy_policy::kSiteLevelsPref), R"({"news.test":"off"})");
}

}  // namespace
}  // namespace ghost::settings
```

- [x] **Step 2: Run it to see it fail.** Expected: every value is `"null"`.
- [x] **Step 3: Patch 0040.** In `PrefsUtil::GetAllowlistedKeys()`, just before `return *s_allowlist;` at the end:

```cpp
  // Shade's settings (patches/0040).
  (*s_allowlist)[ghost::privacy_policy::kDefaultLevelPref] =
      settings_api::PrefType::kString;
  (*s_allowlist)[ghost::privacy_policy::kSiteLevelsPref] =
      settings_api::PrefType::kDictionary;
  (*s_allowlist)[ghost::query_filter::kStripCampaignParametersPref] =
      settings_api::PrefType::kBoolean;
  (*s_allowlist)[ghost::kGpcEnabledPref] = settings_api::PrefType::kBoolean;
```

  Add the three includes and the deps `//ghost/browser/privacy_policy`, `//ghost/browser/query_filter:prefs`, `//ghost/browser/features` to the target that lists `prefs_util.cc`. If `gn check` reports a cycle, move the four constants into a header-only `source_set("pref_names")` in `//ghost/browser/prefs` and depend on that instead. Commit with `-s`. `Why:` chrome://settings reads and writes only allow-listed prefs. `Upstream:` not upstreamable, product-specific.
- [x] **Step 4: Run the test.** Expected: pass.
- [x] **Step 5: Commit.** In webops: `settings: Shade's prefs reach chrome://settings (patch 0040)`. Re-export; `patches.py check`.

### Task 5: The handler for the site list (done, then dropped in Task 7: see the progress notes)

**Files:**
- Create: `browser/ui/settings/protections_handler.{h,cc}`
- Modify: `browser/ui/settings/settings_page.cc` (adds the handler), `browser/ui/settings/BUILD.gn` (deps `//chrome/browser/ui/webui/settings:settings_page_ui_handler` or the target that has `settings_page_ui_handler.h`, `//ghost/browser/privacy_policy`, `//ghost/components/site`)
- Test: `browser/ui/settings/protections_handler_unittest.cc` (add to `ghost_unittests`)

- [x] **Step 1: Write the failing tests.**

```cpp
class ProtectionsHandlerTest : public ChromeRenderViewHostTestHarness {
 protected:
  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    web_ui_.set_web_contents(web_contents());
    auto handler = std::make_unique<ProtectionsHandler>(profile());
    handler->set_web_ui(&web_ui_);
    handler->AllowJavascriptForTesting();
    web_ui_.AddMessageHandler(std::move(handler));
  }

  void Send(const std::string& message, base::Value::List args) {
    web_ui_.HandleReceivedMessage(message, args);
  }

  const base::Value::Dict& Levels() {
    return profile()->GetPrefs()->GetDict(privacy_policy::kSiteLevelsPref);
  }

  content::TestWebUI web_ui_;
};

TEST_F(ProtectionsHandlerTest, SetsASitesLevel) {
  Send("shadeSetSiteLevel", base::Value::List().Append("news.test").Append("strict"));
  EXPECT_EQ(*Levels().FindString("news.test"), "strict");
}

TEST_F(ProtectionsHandlerTest, KeysByRegistrableDomain) {
  Send("shadeSetSiteLevel", base::Value::List().Append("www.news.test").Append("off"));
  EXPECT_TRUE(Levels().contains("news.test"));
  EXPECT_FALSE(Levels().contains("www.news.test"));
}

TEST_F(ProtectionsHandlerTest, RefusesAnUnknownLevelOrNoSite) {
  Send("shadeSetSiteLevel", base::Value::List().Append("news.test").Append("strictest"));
  Send("shadeSetSiteLevel", base::Value::List().Append("").Append("off"));
  Send("shadeSetSiteLevel", base::Value::List().Append("a b").Append("off"));
  EXPECT_TRUE(Levels().empty());
}

TEST_F(ProtectionsHandlerTest, ClearsASite) {
  Send("shadeSetSiteLevel", base::Value::List().Append("news.test").Append("off"));
  Send("shadeClearSiteLevel", base::Value::List().Append("news.test"));
  EXPECT_FALSE(Levels().contains("news.test"));
}
```

- [x] **Step 2: Run them to see them fail.** Expected: a compile failure.
- [x] **Step 3: Implement.**

```cpp
namespace ghost::settings {

// chrome://settings' writes to the site list of Shade's section. Reads go
// through settingsPrivate (patches/0040); writes come here so that the level
// and the site are checked, and the site keyed, as the panel does.
class ProtectionsHandler : public ::settings::SettingsPageUIHandler {
 public:
  explicit ProtectionsHandler(Profile* profile);
  ProtectionsHandler(const ProtectionsHandler&) = delete;
  ProtectionsHandler& operator=(const ProtectionsHandler&) = delete;
  ~ProtectionsHandler() override;

  // ::settings::SettingsPageUIHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override {}
  void OnJavascriptDisallowed() override {}

 private:
  // [site, level]: the site is a host; it's keyed by its registrable domain.
  void HandleSetSiteLevel(const base::Value::List& args);
  // [site]
  void HandleClearSiteLevel(const base::Value::List& args);

  const raw_ptr<Profile> profile_;
};

}  // namespace ghost::settings
```

  In the `.cc`, a helper turns a site into a page URL, or an empty `GURL` when it isn't one:

```cpp
GURL SitePage(const base::Value& site) {
  if (!site.is_string()) {
    return GURL();
  }
  GURL page("https://" + site.GetString() + "/");
  if (!page.is_valid() || page.host().empty() || page.host() != site.GetString()) {
    return GURL();
  }
  return page;
}
```

  `HandleSetSiteLevel` needs two args, a valid page and `privacy_policy::ParseProtectionLevel(args[1].GetString())`. It then calls `privacy_policy::SetLevel(profile_, page, *level)`, which keys by registrable domain. `HandleClearSiteLevel` calls `ClearLevel`. Register both with `web_ui()->RegisterMessageCallback("shadeSetSiteLevel", base::BindRepeating(&ProtectionsHandler::HandleSetSiteLevel, base::Unretained(this)))`, and the same for `shadeClearSiteLevel`.

  `SetUpSettingsPage` adds `web_ui->AddMessageHandler(std::make_unique<ProtectionsHandler>(Profile::FromWebUI(web_ui)))`.
- [x] **Step 4: Run the tests.** Expected: pass.
- [x] **Step 5: Commit.** `settings: the handler for Shade's site list`.

### Task 6: The strings

**Files:**
- Create: `browser/ui/settings/settings_strings.{h,cc}`
- Modify: `browser/ui/settings/settings_page.cc`

- [x] **Step 1:** Write `settings_strings.cc` with `void AddSettingsStrings(content::WebUIDataSource* source)`. It makes these `AddString` calls, English only. The level sentences repeat the panel's in `protections_strings.cc`: refer to those constants rather than copying them.

| Key | Text |
|---|---|
| `shadeProtectionsTitle` | Shade protections |
| `shadeProtectionsLead` | Choose how much Shade blocks on the sites you visit. You can change it for any site from the shield in the toolbar. |
| `shadeDefaultLevel` | Default level |
| `shadeLevelOff`, `shadeLevelStandard`, `shadeLevelStrict` | Off, Standard, Strict |
| `shadeLevelOffSummary` | Nothing is blocked. Cookie, HTTPS and GPC protections still apply. |
| `shadeLevelOffWarning` | Trackers and ads load on every site you haven't set. |
| `shadeLevelStandardSummary` | Balanced for privacy and for sites that work. |
| `shadeLevelStrictSummary` | Stronger protection; some sites may break. |
| `shadeLevelOffDetails`, `shadeLevelStandardDetails`, `shadeLevelStrictDetails` | the panel's three sentences |
| `shadeRecommended` | Recommended |
| `shadeIncognitoNote` | Incognito windows always start at Strict. |
| `shadeMoreProtections` | More protections |
| `shadeGpc` | Send Global Privacy Control |
| `shadeGpcSub` | Tells sites not to sell or share your data. |
| `shadeRelaunch` | Relaunch |
| `shadeCampaign` | Remove campaign parameters |
| `shadeCampaignSub` | utm_source, utm_medium and the rest. Always removed in Incognito and at Strict. |
| `shadeSites` | Sites with their own level |
| `shadeSitesEmpty` | Sites you set from the shield in the toolbar appear here. |
| `shadeRemoveSite` | Remove $1 |
| `shadeSiteLevel` | Level for $1 |

  Also `source->AddBoolean("shadeGpcEnabledAtStartup", blink::IsGlobalPrivacyControlEnabled());` (dep `//third_party/blink/public/common`).
- [x] **Step 2:** `SetUpSettingsPage` calls `AddSettingsStrings(source)` instead of its single string. Build. Expected: compiles; the spike's title still shows.
- [x] **Step 3: Commit.** `settings: the strings of Shade's section`.

### Task 7: The section

**Files:**
- Create: `browser/resources/settings/level_card.{ts,html.ts,css}`, `site_list.{ts,html.ts,css}`, `protections_browser_proxy.ts`, `protections_page.css`
- Modify: `protections_page.ts`, `protections_page.html.ts`, `BUILD.gn` (`ts_files`, `css_files`)

- [x] **Step 1:** `protections_browser_proxy.ts`:

```ts
export interface ProtectionsBrowserProxy {
  setSiteLevel(site: string, level: string): void;
  clearSiteLevel(site: string): void;
}

export class ProtectionsBrowserProxyImpl implements ProtectionsBrowserProxy {
  setSiteLevel(site: string, level: string) {
    chrome.send('shadeSetSiteLevel', [site, level]);
  }

  clearSiteLevel(site: string) {
    chrome.send('shadeClearSiteLevel', [site]);
  }

  static getInstance(): ProtectionsBrowserProxy {
    return instance || (instance = new ProtectionsBrowserProxyImpl());
  }

  static setInstance(obj: ProtectionsBrowserProxy) {
    instance = obj;
  }
}

let instance: ProtectionsBrowserProxy|null = null;
```

- [x] **Step 2:** `level_card.ts` defines `shade-level-card`, a `CrLitElement` with these properties:
  - `level` (`'off'|'standard'|'strict'`), `checked` (boolean, reflected), `recommended` (boolean);
  - `name`, `summary`, `details` (the sentence of the panel), `warning` (optional string).

  It renders:
  - a `cr-radio-button` (`name=${level}`, `?checked=${checked}`) whose label is the name, followed by the "Recommended" chip when `recommended`;
  - the summary;
  - and when `checked`, the details as a `<ul>`, one `<li>` per sentence split on `. `, plus the warning in a `.warning` row.

  The card is the radio group's item: the page wraps the three cards in a `cr-radio-group` (`selectable-elements="shade-level-card"`) and listens to `selected-changed`.

  Style (`level_card.css`):
  - `border: 1.5px solid var(--cr-separator-color); border-radius: 14px; padding: 12px 14px; background: var(--cr-card-background-color);`;
  - when checked, `border-color: var(--cr-checked-color); box-shadow: 0 0 0 3px color-mix(in srgb, var(--cr-checked-color) 20%, transparent);`;
  - the chip uses `--cr-checked-color` on a 15% mix of it.

  There are no literal colours.
- [x] **Step 3:** `site_list.ts` defines `shade-site-list`, using `PrefServiceObserverMixinLit(CrLitElement)`:
  - `connectedCallback` calls `this.addPrefObserver('ghost.privacy_policy.site_levels', pref => this.sites_ = Object.entries(pref.value as Record<string, string>).sort(([a], [b]) => a.localeCompare(b)))`.
  - Each row:
    - a favicon (`style="background-image:${getFaviconForPageURL('https://' + site + '/', false)}"`);
    - the domain;
    - a `<select class="md-select">` with the three levels, which calls `setSiteLevel` on change and has `aria-label=${i18n('shadeSiteLevel', site)}`;
    - a `cr-icon-button` (`class="icon-clear"`), which calls `clearSiteLevel`, labelled `shadeRemoveSite`.
  - With no rows: `shadeSitesEmpty`.

  The list doesn't change its own state: the pref observer redraws it when the browser has written.
- [x] **Step 4:** `protections_page.ts` uses `PrefServiceObserverMixinLit(CrLitElement)` and renders, in order:
  - the header (`h1` and lead);
  - the "Default level" heading;
  - the radio group of three cards, bound to `default_level` through `PrefService.getInstance().setPrefValue('ghost.privacy_policy.default_level', level)`, with Standard `recommended` and Off with the warning;
  - the Incognito note;
  - the "More protections" group: two rows (a title and subtitle, then a `cr-toggle` bound to its pref with `setPrefValue`). The GPC row shows a `cr-button` "Relaunch" while `gpc.value !== loadTimeData.getBoolean('shadeGpcEnabledAtStartup')`, and it calls `LifetimeBrowserProxyImpl.getInstance().relaunch()` (from `/shared/settings/lifetime_browser_proxy.js`);
  - `shade-site-list` under the "Sites with their own level" heading.

  It mirrors the three prefs with `mirrorPrefs({...})` and awaits `PrefService.getInstance().whenInitialized()` before the first render of the controls. `searchContents` stays as in Task 1.

  Style (`protections_page.css`):
  - headings `font-size: 22px; font-weight: 600` (title) and `12px` uppercase muted labels (sections), as in the approved mockup (`.superpowers/brainstorm/755-1791704169/content/style.html`, choice A);
  - groups `border-radius: 14px` on `--cr-card-background-color`, rows separated by `--cr-separator-color`;
  - colours only from `--cr-*` and `--color-sys-*`.
- [x] **Step 5:** Build and check by hand:
  - choosing Strict writes `ghost.privacy_policy.default_level` (`chrome://prefs-internals`);
  - setting a site from the panel shows it in the list without reloading Settings;
  - the list's menu changes it and ✕ removes it;
  - turning GPC off shows Relaunch;
  - search "tracker" highlights the Standard card's text.
- [x] **Step 6: Commit.** `settings: Shade's protections section`.

### Task 8: The panel's link

**Files:**
- Modify: `browser/ui/protections/protections.mojom` (`PageHandler.OpenSettings()`), `protections_page_handler.{h,cc}`, `protections_strings.cc` (`kSettingsLink` "Protection settings"), `browser/resources/protections/app.html.ts`, `app.ts`, `app.css`

- [x] **Step 1:** `OpenSettings()` in the handler:

```cpp
void ProtectionsPageHandler::OpenSettings() {
  content::WebContents* tab = ActiveTab();
  if (!tab) {
    return;
  }
  tab->OpenURL(content::OpenURLParams(GURL("chrome://settings/protections"), content::Referrer(),
                                      WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                      ui::PAGE_TRANSITION_LINK, /*is_renderer_initiated=*/false),
               /*navigation_handle_callback=*/{});
  if (ui_->embedder()) {
    ui_->embedder()->CloseUI();
  }
}
```

  Use the handler's existing way of finding the bubble's tab in place of `ActiveTab()` if it's named differently.
- [x] **Step 2:** The page shows a link row at the bottom, `<a href="#" id="settings">${i18n('settingsLink')}</a>`, which calls `handler.openSettings()` and prevents the default.
- [x] **Step 3:** Build, open the panel, click the link. Expected: a new tab at `chrome://settings/protections`; the bubble closes.
- [x] **Step 4: Commit.** `protections: the panel links to Shade's settings`.

### Task 9: Browser tests for the section

**Files:**
- Modify: `browser/ui/settings/protection_settings_browsertest.cc`, `browser/ui/protections/protections_panel_browsertest.cc`

- [ ] **Step 1:** Add a JS helper that finds an element through shadow roots, and a poll:

```cpp
// The first element matching |selector| in the page, looking inside every
// shadow root (chrome://settings nests its pages several roots deep).
constexpr char kFindDeep[] = R"(function findDeep(selector, root = document) {
  const found = root.querySelector(selector);
  if (found) return found;
  for (const host of root.querySelectorAll('*')) {
    if (host.shadowRoot) {
      const inner = findDeep(selector, host.shadowRoot);
      if (inner) return inner;
    }
  }
  return null;
}
function waitDeep(selector) {
  return new Promise(done => {
    const poll = () => { const e = findDeep(selector); e ? done(e) : setTimeout(poll, 50); };
    poll();
  });
})";
```

  Each test runs `content::EvalJs(Tab(), base::StrCat({kFindDeep, "; (async () => { ... })()"}))`.
- [ ] **Step 2:** Write the tests, each opening `chrome://settings/protections` first:
  1. `TheSectionIsThere`: `waitDeep('shade-protections-page')`; three `shade-level-card`s in its shadow root; Standard checked; two `cr-toggle`s; `shade-site-list` showing the empty text.
  2. `ChoosingStrictWritesTheDefault`: click the Strict card's radio; poll the pref in C++ until `GetString(kDefaultLevelPref) == "strict"`.
  3. `TheListFollowsThePanel`: `SetLevel(profile, https://news.test/, kOff)` after the page loaded; `waitDeep` for a row containing `news.test`; click its remove button; poll until the dict is empty.
  4. `TheCampaignSwitchWritesItsPref`: click the second `cr-toggle`; poll `kStripCampaignParametersPref == true`.
  5. `TurningGpcOffOffersARelaunch`: click the first `cr-toggle`; `waitDeep('#relaunch')` is visible; `local_state` has `kGpcEnabledPref == false`. Turn it back on in the test, so `PRE_`-less tests keep the default.
  6. `SearchFindsTheSection`: navigate to `chrome://settings/?search=tracker`; wait for a `.search-highlight-hit` inside `shade-protections-page`'s shadow root; the element has no `hidden-by-search` attribute.

  In `protections_panel_browsertest.cc`:
  7. `TheLinkOpensTheSettings`: open the bubble, click `#settings` in it, and wait (with `ui_test_utils::TabAddedWaiter`) for a tab at `chrome://settings/protections`.
  8. `AnOffDefaultTurnsTheButtonOff`: set `kDefaultLevelPref` to `"off"`, open a page with three tracker images; the trackers load, the badge text is empty and the accessible name starts with "Protections: Off".
- [ ] **Step 3:** Run `out\vanilla\ghost_browsertests --gtest_filter=ProtectionSettings*:ProtectionsPanel*`. Expected: pass. Then all of `ghost_browsertests` and `ghost_unittests`, with no retry.
- [ ] **Step 4: Commit.** `settings: browser tests for Shade's section`.

### Task 10: The look, with the user

- [ ] Run `out\vanilla\chrome.exe` with a fresh `--user-data-dir` in the session scratchpad. Set two sites from the panel first, so the list isn't empty.
- [ ] The user checks the section in light and dark (`--force-dark-mode`), at 100% and 150% Windows scaling, and with a search.
- [ ] Fix what the user asks for, rebuild, and show it again.
- [ ] Done when the user approves. Record the approval in the progress notes.

### Task 11: The official build, mutation checks, audit, docs, push

- [ ] **The official build.** Ask the user before starting it: it takes hours, and the release workflow owns `out\release`. Build `chrome` in `out\release` with `release.gn`, launched through WMI as in the release runbook, with `chrome/VERSION` untouched. Open `chrome://settings/protections` in it. Expected:
  - the section renders;
  - search finds it;
  - DevTools' network panel shows no request for `/shade/`, since everything is bundled.

  Record the build time.
- [ ] **Mutation checks.** Each must make at least one test fail; check that the mutant built before trusting a green run. Undo each with `sync.py`, then compare `src/ghost` with webops:
  - **M1:** `ModeDefault` returns Standard for a regular profile whatever the pref says.
  - **M2:** `ModeDefault` reads the pref off the record too.
  - **M3:** `AppendFeatureOverrides` forces GPC whatever the pref says.
  - **M4:** the list's remove button writes the dictionary back unchanged (amended in Task 7: the handler M4 targeted was dropped).
  - **M5:** patch 0040 without the site-levels line (a change to `prefs_util.cc` in `chromium/src`, reverted with `git checkout` there).
- [ ] **Full suites and the audit.** Run `ghost_unittests` and `ghost_browsertests` in full. Run the egress audit. Expected: 0 unexpected hosts.
- [ ] **Docs.**
  - `privacy-model.md`: the default level; GPC can be turned off, at the next start; where the settings live.
  - `architecture.md`: the Settings library, its mapping and patches 0038–0039.
  - `testing.md`.
  - `roadmap.md`: 3D-4 done, 3D closed.
  - The progress notes; the status of the spec and of this plan.
- [ ] **Push.** Ask the user before pushing. After the push, wait for both tooling jobs; then update the memory.
