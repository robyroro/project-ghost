// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <string_view>

#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
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

class ProtectionSettingsBrowserTest : public InProcessBrowserTest {
 protected:
  content::WebContents* Tab() { return browser()->tab_strip_model()->GetActiveWebContents(); }

  std::string GetPref(std::string_view key) {
    return content::EvalJs(Tab(), content::JsReplace(base::StrCat({kGetPref, "($1)"}), key))
        .ExtractString();
  }
};

IN_PROC_BROWSER_TEST_F(ProtectionSettingsBrowserTest, SettingsSeesShadesPrefs) {
  privacy_policy::SetLevel(GetProfile(), GURL("https://news.test/"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/protections")));
  EXPECT_EQ(GetPref(privacy_policy::kDefaultLevelPref), "\"standard\"");
  EXPECT_EQ(GetPref(query_filter::kStripCampaignParametersPref), "false");
  EXPECT_EQ(GetPref(kGpcEnabledPref), "true");
  EXPECT_EQ(GetPref(privacy_policy::kSiteLevelsPref), R"({"news.test":"off"})");
}

}  // namespace
}  // namespace ghost::settings
