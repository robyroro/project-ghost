// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Google services that the egress audit found running on a fresh profile, and
// that hook patches turn off (docs/roadmap.md, Phase 1 step 6). The audit
// shows that nothing is sent; these tests name the mechanism behind each fix,
// so an upstream change that breaks one fails here and not as an unexplained
// new host. Services turned off by a pref default or a feature override are
// tested alongside those (browser/prefs, browser/features).

#include <string>

#include "base/command_line.h"
#include "base/test/test_future.h"
#include "chrome/browser/gcm/gcm_profile_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/search_test_utils.h"
#include "components/gcm_driver/fake_gcm_app_handler.h"
#include "components/gcm_driver/gcm_client.h"
#include "components/gcm_driver/gcm_driver.h"
#include "components/gcm_driver/gcm_profile_service.h"
#include "components/regional_capabilities/regional_capabilities_switches.h"
#include "components/search_engines/search_engine_type.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost {
namespace {

class GoogleServicesBrowserTest : public InProcessBrowserTest {
 protected:
  TemplateURLService* LoadedTemplateUrlService() {
    TemplateURLService* service =
        TemplateURLServiceFactory::GetForProfile(GetProfile());
    search_test_utils::WaitForTemplateURLServiceToLoad(service);
    return service;
  }

  SearchEngineType DefaultSearchEngineType() {
    TemplateURLService* service = LoadedTemplateUrlService();
    const TemplateURL* engine = service->GetDefaultSearchProvider();
    return engine ? engine->GetEngineType(service->search_terms_data())
                  : SEARCH_ENGINE_UNKNOWN;
  }
};

// GCM checks in with Google the first time anything registers with it. A
// registration must fail without starting the client (patches/0006).
IN_PROC_BROWSER_TEST_F(GoogleServicesBrowserTest, CloudMessagingDoesNotStart) {
  gcm::GCMDriver* driver =
      gcm::GCMProfileServiceFactory::GetForProfile(GetProfile())->driver();
  const std::string app_id = "ghost-browsertest";
  gcm::FakeGCMAppHandler handler;
  driver->AddAppHandler(app_id, &handler);

  base::test::TestFuture<std::string, gcm::GCMClient::Result> registration;
  driver->Register(
      app_id, {"sender"},
      registration.GetCallback<const std::string&, gcm::GCMClient::Result>());

  EXPECT_EQ(registration.Get<gcm::GCMClient::Result>(),
            gcm::GCMClient::GCM_DISABLED);
  EXPECT_FALSE(driver->IsStarted());
  driver->RemoveAppHandler(app_id);
}

// With Google as the default search engine, the New Tab page loads Google's
// logo, bar and promos (patches/0007).
IN_PROC_BROWSER_TEST_F(GoogleServicesBrowserTest,
                       DefaultSearchEngineIsDuckDuckGo) {
  EXPECT_EQ(DefaultSearchEngineType(), SEARCH_ENGINE_DUCKDUCKGO);
  EXPECT_FALSE(search::DefaultSearchProviderIsGoogle(GetProfile()));
}

// DuckDuckGo defines a remote new tab page, which would be loaded on every
// new tab (patches/0008).
IN_PROC_BROWSER_TEST_F(GoogleServicesBrowserTest, NewTabPageIsLocal) {
  LoadedTemplateUrlService();
  EXPECT_EQ(search::GetNewTabPageURL(GetProfile()),
            GURL(chrome::kChromeUINewTabPageThirdPartyURL));
}

// South Korea's regional list does not include DuckDuckGo; upstream would fall
// back to the list's first engine, which is Google.
class GoogleServicesOutsideDuckDuckGoRegionsBrowserTest
    : public GoogleServicesBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitchASCII(switches::kSearchEngineChoiceCountry, "KR");
  }
};

IN_PROC_BROWSER_TEST_F(GoogleServicesOutsideDuckDuckGoRegionsBrowserTest,
                       DefaultSearchEngineIsDuckDuckGo) {
  EXPECT_EQ(DefaultSearchEngineType(), SEARCH_ENGINE_DUCKDUCKGO);
}

}  // namespace
}  // namespace ghost
