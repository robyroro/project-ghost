// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/query_filter/prefs.h"

#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::query_filter {
namespace {

// Registered through chrome's RegisterProfilePrefs() (patches/0032), off:
// a regular profile keeps campaign parameters until the user asks.
TEST(QueryFilterPrefsTest, CampaignParametersAreKeptByDefault) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfile profile;
  const PrefService::Preference* pref =
      profile.GetPrefs()->FindPreference(kStripCampaignParametersPref);
  ASSERT_TRUE(pref);
  EXPECT_TRUE(pref->IsDefaultValue());
  EXPECT_FALSE(profile.GetPrefs()->GetBoolean(kStripCampaignParametersPref));
}

}  // namespace
}  // namespace ghost::query_filter
