// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <string_view>

#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/browser/features/local_state_prefs.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/query_filter/prefs.h"
#include "url/gurl.h"

namespace ghost::settings {
namespace {

// Reads a pref the way chrome://settings does; "null" when Settings may not
// see it.
constexpr char kGetPref[] = R"((key => new Promise(done =>
    chrome.settingsPrivate.getPref(key, pref => done(pref ? JSON.stringify(pref.value) : 'null')))))";

// Helpers for the section, which chrome://settings nests several shadow roots
// deep: findDeep looks inside every root, waitFor polls until a condition
// holds, section() waits for Shade's section to render.
constexpr char kHelpers[] = R"(
function findDeep(selector, root = document) {
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
function waitFor(check) {
  return new Promise(done => {
    const poll = () => { const value = check(); value ? done(value) : setTimeout(poll, 20); };
    poll();
  });
}
async function section() {
  const page = await waitFor(() => findDeep('shade-protections-page'));
  await page.updateComplete;
  return page.shadowRoot;
}
)";

class ProtectionSettingsBrowserTest : public InProcessBrowserTest {
 protected:
  content::WebContents* Tab() { return browser()->tab_strip_model()->GetActiveWebContents(); }

  std::string GetPref(std::string_view key) {
    return content::EvalJs(Tab(), content::JsReplace(base::StrCat({kGetPref, "($1)"}), key))
        .ExtractString();
  }

  // Runs |body|, the body of an async function with the helpers in scope.
  content::EvalJsResult Run(std::string_view body) {
    return content::EvalJs(Tab(), base::StrCat({kHelpers, "(async () => {", body, "})()"}));
  }

  void OpenSection() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/protections")));
  }

  PrefService* Prefs() { return GetProfile()->GetPrefs(); }
};

IN_PROC_BROWSER_TEST_F(ProtectionSettingsBrowserTest, SettingsSeesShadesPrefs) {
  privacy_policy::SetLevel(GetProfile(), GURL("https://news.test/"),
                           privacy_policy::ProtectionLevel::kOff);
  OpenSection();
  EXPECT_EQ(GetPref(privacy_policy::kDefaultLevelPref), "\"standard\"");
  EXPECT_EQ(GetPref(query_filter::kStripCampaignParametersPref), "false");
  EXPECT_EQ(GetPref(kGpcEnabledPref), "true");
  EXPECT_EQ(GetPref(privacy_policy::kSiteLevelsPref), R"({"news.test":"off"})");
}

IN_PROC_BROWSER_TEST_F(ProtectionSettingsBrowserTest, TheSectionIsThere) {
  OpenSection();
  EXPECT_EQ(Run(R"(
    const root = await section();
    const cards = [...root.querySelectorAll('cr-radio-button.card')];
    await waitFor(() => cards.some(card => card.checked));
    return [
      cards.map(card => card.name + (card.checked ? '*' : '')).join(' '),
      root.querySelectorAll('cr-toggle').length,
      (await waitFor(() => root.querySelector('shade-site-list').shadowRoot
          .querySelector('.empty'))).textContent.trim(),
    ].join(' | ');)"),
            "off standard* strict | 2 | Sites you set from the shield in the toolbar appear here.");
}

IN_PROC_BROWSER_TEST_F(ProtectionSettingsBrowserTest, ChoosingStrictWritesTheDefault) {
  OpenSection();
  ASSERT_TRUE(content::ExecJs(Tab(), base::StrCat({kHelpers, R"((async () => {
    const root = await section();
    root.querySelector('cr-radio-button[name=strict]').click();
  })())"})));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return Prefs()->GetString(privacy_policy::kDefaultLevelPref) == "strict"; }));
}

IN_PROC_BROWSER_TEST_F(ProtectionSettingsBrowserTest, TheListFollowsThePanel) {
  OpenSection();
  ASSERT_EQ(Run("await section(); return 'ready';"), "ready");
  // As the panel would: the page is open meanwhile.
  privacy_policy::SetLevel(GetProfile(), GURL("https://www.news.test/"),
                           privacy_policy::ProtectionLevel::kOff);
  EXPECT_EQ(Run(R"(
    const list = (await section()).querySelector('shade-site-list').shadowRoot;
    const row = await waitFor(() => list.querySelector('.site'));
    return row.querySelector('.name').textContent + ' ' + row.querySelector('select').value;)"),
            "news.test off");

  // The list's menu changes the level; its remove button clears the site.
  ASSERT_TRUE(content::ExecJs(Tab(), base::StrCat({kHelpers, R"((async () => {
    const list = (await section()).querySelector('shade-site-list').shadowRoot;
    const select = list.querySelector('select');
    select.value = 'strict';
    select.dispatchEvent(new Event('change'));
  })())"})));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const std::string* level = Prefs()->GetDict(privacy_policy::kSiteLevelsPref).FindString("news.test");
    return level && *level == "strict";
  }));
  ASSERT_TRUE(content::ExecJs(Tab(), base::StrCat({kHelpers, R"((async () => {
    const list = (await section()).querySelector('shade-site-list').shadowRoot;
    list.querySelector('cr-icon-button.remove').click();
  })())"})));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return Prefs()->GetDict(privacy_policy::kSiteLevelsPref).empty(); }));
  EXPECT_EQ(privacy_policy::GetLevel(GetProfile(), GURL("https://news.test/")),
            privacy_policy::ProtectionLevel::kStandard);
}

IN_PROC_BROWSER_TEST_F(ProtectionSettingsBrowserTest, TheCampaignSwitchWritesItsPref) {
  OpenSection();
  ASSERT_TRUE(content::ExecJs(Tab(), base::StrCat({kHelpers, R"((async () => {
    (await section()).querySelector('#campaign').click();
  })())"})));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return Prefs()->GetBoolean(query_filter::kStripCampaignParametersPref); }));
}

IN_PROC_BROWSER_TEST_F(ProtectionSettingsBrowserTest, TurningGpcOffOffersARelaunch) {
  OpenSection();
  EXPECT_EQ(Run(R"(
    const root = await section();
    const before = !!root.querySelector('#relaunch');
    root.querySelector('#gpc').click();
    await waitFor(() => root.querySelector('#relaunch'));
    return before;)"),
            false);
  PrefService* local_state = g_browser_process->local_state();
  EXPECT_TRUE(base::test::RunUntil([&] { return !local_state->GetBoolean(kGpcEnabledPref); }));
  // Back on, as the browser started: no relaunch to offer.
  EXPECT_EQ(Run(R"(
    const root = await section();
    root.querySelector('#gpc').click();
    await waitFor(() => !root.querySelector('#relaunch'));
    return 'hidden';)"),
            "hidden");
  EXPECT_TRUE(base::test::RunUntil([&] { return local_state->GetBoolean(kGpcEnabledPref); }));
}

IN_PROC_BROWSER_TEST_F(ProtectionSettingsBrowserTest, SearchFindsTheSection) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/?search=tracker")));
  EXPECT_EQ(Run(R"(
    const page = await waitFor(() => findDeep('shade-protections-page'));
    const hit = await waitFor(
        () => page.shadowRoot.querySelector('.search-highlight-hit'));
    return hit.textContent + ' ' + page.hasAttribute('hidden-by-search');)"),
            "tracker false");
}

}  // namespace
}  // namespace ghost::settings
