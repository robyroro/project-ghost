// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Feature overrides only take effect through chrome's real startup sequence
// (ChromeFeatureListCreator), so these run as browser tests rather than unit
// tests, and assert the resulting behaviour rather than the feature state.

#include "base/command_line.h"
#include "chrome/browser/autocomplete/aim_eligibility_service_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/https_first_mode_settings_tracker.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/autofill/content/browser/content_autofill_client.h"
#include "components/autofill/core/browser/crowdsourcing/autofill_crowdsourcing_manager.h"
#include "components/network_time/network_time_tracker.h"
#include "components/omnibox/browser/aim_eligibility_service.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

using FeatureOverridesBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(FeatureOverridesBrowserTest,
                       HttpsFirstIsBalancedByDefault) {
  EXPECT_EQ(HttpsFirstModeServiceFactory::GetForProfile(GetProfile())
                ->GetCurrentSetting(),
            HttpsFirstModeSetting::kEnabledBalanced);
}

IN_PROC_BROWSER_TEST_F(FeatureOverridesBrowserTest, MediaRouterIsDisabled) {
  EXPECT_FALSE(media_router::MediaRouterEnabled(GetProfile()));
}

IN_PROC_BROWSER_TEST_F(FeatureOverridesBrowserTest, NetworkTimeIsNotQueried) {
  EXPECT_FALSE(g_browser_process->network_time_tracker()
                   ->AreTimeFetchesEnabled());
}

IN_PROC_BROWSER_TEST_F(FeatureOverridesBrowserTest, AiModeIsDisabled) {
  AimEligibilityService* service =
      AimEligibilityServiceFactory::GetForProfile(GetProfile());
  ASSERT_TRUE(service);
  EXPECT_FALSE(service->IsAimAllowedByFeatureAndPolicy());
}

IN_PROC_BROWSER_TEST_F(FeatureOverridesBrowserTest,
                       AutofillDoesNotQueryGoogle) {
  autofill::ContentAutofillClient* client =
      autofill::ContentAutofillClient::FromWebContents(
          chrome_test_utils::GetActiveWebContents(this));
  ASSERT_TRUE(client);
  EXPECT_FALSE(client->GetCrowdsourcingManager().IsEnabled());
}

// Our overrides are defaults, not locks: an explicit --disable-features must
// still turn them off, or developers and users could not opt out.
class FeatureOverridesCommandLineBrowserTest : public InProcessBrowserTest {
 public:
  // Browser tests normally forbid feature switches in favour of
  // ScopedFeatureList, which would bypass the path under test: the switch
  // must reach ChromeFeatureListCreator through the real command line.
  FeatureOverridesCommandLineBrowserTest() { SetAllowFeaturesSwitches(true); }

 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitchASCII("disable-features",
                                    "HttpsFirstBalancedModeAutoEnable");
  }
};

IN_PROC_BROWSER_TEST_F(FeatureOverridesCommandLineBrowserTest,
                       CommandLineStillWins) {
  EXPECT_EQ(HttpsFirstModeServiceFactory::GetForProfile(GetProfile())
                ->GetCurrentSetting(),
            HttpsFirstModeSetting::kDisabled);
}

}  // namespace
}  // namespace ghost
