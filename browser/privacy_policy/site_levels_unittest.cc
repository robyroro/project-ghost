// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/privacy_policy/site_levels.h"

#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::privacy_policy {
namespace {

class SiteLevelsTest : public testing::Test {
 protected:
  Profile* Incognito() { return profile_.GetPrimaryOTRProfile(/*create_if_needed=*/true); }

  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

TEST_F(SiteLevelsTest, EachModeHasItsDefault) {
  EXPECT_EQ(GetLevel(&profile_, GURL("https://a.test/")), ProtectionLevel::kStandard);
  EXPECT_EQ(GetLevel(Incognito(), GURL("https://a.test/")), ProtectionLevel::kStrict);
}

TEST_F(SiteLevelsTest, AChoiceIsTheWholeSites) {
  SetLevel(&profile_, GURL("https://www.a.test/page"), ProtectionLevel::kOff);
  EXPECT_EQ(GetLevel(&profile_, GURL("https://shop.a.test/")), ProtectionLevel::kOff);
  EXPECT_EQ(GetLevel(&profile_, GURL("http://a.test/x")), ProtectionLevel::kOff);
  EXPECT_EQ(GetLevel(&profile_, GURL("https://b.test/")), ProtectionLevel::kStandard);
  ClearLevel(&profile_, GURL("https://a.test/"));
  EXPECT_EQ(GetLevel(&profile_, GURL("https://www.a.test/")), ProtectionLevel::kStandard);
}

TEST_F(SiteLevelsTest, IncognitoStartsFromTheRegularProfilesChoices) {
  SetLevel(&profile_, GURL("https://a.test/"), ProtectionLevel::kOff);
  EXPECT_EQ(GetLevel(Incognito(), GURL("https://a.test/")), ProtectionLevel::kOff);
}

TEST_F(SiteLevelsTest, IncognitosChoicesStayInIncognito) {
  SetLevel(Incognito(), GURL("https://a.test/"), ProtectionLevel::kOff);
  EXPECT_EQ(GetLevel(Incognito(), GURL("https://a.test/")), ProtectionLevel::kOff);
  EXPECT_EQ(GetLevel(&profile_, GURL("https://a.test/")), ProtectionLevel::kStandard);
  EXPECT_FALSE(profile_.GetPrefs()->GetDict(kSiteLevelsPref).contains("a.test"));
}

TEST_F(SiteLevelsTest, AnUnknownStoredLevelIsIgnored) {
  ScopedDictPrefUpdate(profile_.GetPrefs(), kSiteLevelsPref)->Set("a.test", "strictest");
  EXPECT_EQ(GetLevel(&profile_, GURL("https://a.test/")), ProtectionLevel::kStandard);
}

TEST_F(SiteLevelsTest, APageWithoutAHostHasTheModesDefault) {
  EXPECT_EQ(GetLevel(&profile_, GURL()), ProtectionLevel::kStandard);
  EXPECT_EQ(GetLevel(Incognito(), GURL("about:blank")), ProtectionLevel::kStrict);
}

TEST_F(SiteLevelsTest, ThePolicyFollowsTheLevel) {
  SetLevel(&profile_, GURL("https://a.test/"), ProtectionLevel::kStrict);
  EXPECT_TRUE(GetPolicy(&profile_, GURL("https://a.test/")).check_same_site);
  EXPECT_FALSE(GetPolicy(&profile_, GURL("https://b.test/")).check_same_site);
}

}  // namespace
}  // namespace ghost::privacy_policy
