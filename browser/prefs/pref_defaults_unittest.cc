// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Each test checks the behaviour the default is meant to produce, read through
// the same API the browser uses, on a profile whose prefs come from chrome's
// real RegisterUserProfilePrefs(). Checking only the stored value would miss
// prefs whose defaults upstream never consults.

#include <string_view>

#include "chrome/browser/content_settings/cookie_settings_factory.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "components/translate/core/browser/translate_prefs.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

class PrefDefaultsTest : public testing::Test {
 protected:
  PrefService* prefs() { return profile_.GetPrefs(); }

  // The behaviour must come from the registered default, not from a value
  // someone stored; otherwise these tests would pass with the hook removed.
  void ExpectUsesDefault(std::string_view pref) {
    const PrefService::Preference* preference = prefs()->FindPreference(pref);
    ASSERT_TRUE(preference) << pref << " is no longer registered upstream";
    EXPECT_TRUE(preference->IsDefaultValue()) << pref;
  }

  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

TEST_F(PrefDefaultsTest, ThirdPartyCookiesAreBlocked) {
  ExpectUsesDefault(prefs::kCookieControlsMode);
  EXPECT_TRUE(CookieSettingsFactory::GetForProfile(&profile_)
                  ->ShouldBlockThirdPartyCookies());
}

TEST_F(PrefDefaultsTest, PreloadingIsOff) {
  ExpectUsesDefault(prefs::kNetworkPredictionOptions);
  EXPECT_EQ(prefetch::GetPreloadPagesState(*prefs()),
            prefetch::PreloadPagesState::kNoPreloading);
}

TEST_F(PrefDefaultsTest, SearchSuggestionsAreOff) {
  ExpectUsesDefault(prefs::kSearchSuggestEnabled);
  EXPECT_FALSE(prefs()->GetBoolean(prefs::kSearchSuggestEnabled));
}

TEST_F(PrefDefaultsTest, BrowserSignInIsNotAllowed) {
  ExpectUsesDefault(prefs::kSigninAllowed);
  EXPECT_FALSE(prefs()->GetBoolean(prefs::kSigninAllowed));
}

TEST_F(PrefDefaultsTest, TranslationIsNotOffered) {
  ExpectUsesDefault(translate::prefs::kOfferTranslateEnabled);
  EXPECT_FALSE(translate::TranslatePrefs(prefs()).IsOfferTranslateEnabled());
}

}  // namespace
}  // namespace ghost
