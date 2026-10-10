// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/panel_state.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/web_contents_tester.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/protections/page_protections.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::protections {
namespace {

using privacy_policy::ProtectionLevel;

class PanelStateTest : public ChromeRenderViewHostTestHarness {
 protected:
  bool HasChoice(const char* site) {
    return profile()->GetPrefs()->GetDict(privacy_policy::kSiteLevelsPref).Find(site);
  }
};

TEST_F(PanelStateTest, ASiteIsItsRegistrableDomain) {
  NavigateAndCommit(GURL("https://shop.example.org/cart"));
  PanelState state = GetPanelState(*web_contents());
  EXPECT_TRUE(state.applies);
  EXPECT_EQ(state.site, "example.org");
  EXPECT_EQ(state.level, ProtectionLevel::kStandard);
  EXPECT_EQ(state.mode_default, ProtectionLevel::kStandard);
  EXPECT_EQ(state.blocked_count, 0);
  EXPECT_FALSE(state.off_the_record);
}

TEST_F(PanelStateTest, ShowsTheSitesChoiceAndTheCount) {
  NavigateAndCommit(GURL("https://news.test/"));
  privacy_policy::SetLevel(profile(), GURL("https://news.test/"), ProtectionLevel::kOff);
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId());
  PanelState state = GetPanelState(*web_contents());
  EXPECT_EQ(state.level, ProtectionLevel::kOff);
  EXPECT_EQ(state.blocked_count, 1);
}

TEST_F(PanelStateTest, ProtectionsApplyToWebPagesOnly) {
  // A chrome: URL without a page: real WebUI pages don't load in this harness.
  for (const char* url :
       {"chrome://no-such-page/", "file:///C:/a.html", "about:blank", "data:text/html,x"}) {
    NavigateAndCommit(GURL(url));
    EXPECT_FALSE(GetPanelState(*web_contents()).applies) << url;
  }
  NavigateAndCommit(GURL("http://plain.test/"));
  EXPECT_TRUE(GetPanelState(*web_contents()).applies);
}

TEST_F(PanelStateTest, ChoosingTheDefaultClearsTheSitesChoice) {
  NavigateAndCommit(GURL("https://www.news.test/"));
  ChooseLevel(*web_contents(), ProtectionLevel::kOff);
  EXPECT_TRUE(HasChoice("news.test"));
  ChooseLevel(*web_contents(), ProtectionLevel::kStandard);
  EXPECT_FALSE(HasChoice("news.test"));
}

TEST_F(PanelStateTest, ChoosingALevelReloadsThePage) {
  NavigateAndCommit(GURL("https://news.test/"));
  ASSERT_FALSE(web_contents()->GetController().GetPendingEntry());
  ChooseLevel(*web_contents(), ProtectionLevel::kStrict);
  content::NavigationEntry* pending = web_contents()->GetController().GetPendingEntry();
  ASSERT_TRUE(pending);
  EXPECT_EQ(pending->GetURL(), GURL("https://news.test/"));
}

TEST_F(PanelStateTest, NothingIsChosenWhereProtectionsDontApply) {
  NavigateAndCommit(GURL("chrome://no-such-page/"));
  ChooseLevel(*web_contents(), ProtectionLevel::kOff);
  EXPECT_TRUE(profile()->GetPrefs()->GetDict(privacy_policy::kSiteLevelsPref).empty());
  EXPECT_FALSE(web_contents()->GetController().GetPendingEntry());
}

TEST_F(PanelStateTest, IncognitoIsStrictByDefault) {
  std::unique_ptr<content::WebContents> incognito = content::WebContentsTester::CreateTestWebContents(
      profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true), nullptr);
  content::NavigationSimulator::NavigateAndCommitFromBrowser(incognito.get(),
                                                             GURL("https://news.test/"));
  PanelState state = GetPanelState(*incognito);
  EXPECT_TRUE(state.off_the_record);
  EXPECT_EQ(state.mode_default, ProtectionLevel::kStrict);
  EXPECT_EQ(state.level, ProtectionLevel::kStrict);
}

}  // namespace
}  // namespace ghost::protections
