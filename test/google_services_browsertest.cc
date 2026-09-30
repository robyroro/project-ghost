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

#include "base/test/test_future.h"
#include "chrome/browser/gcm/gcm_profile_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/gcm_driver/fake_gcm_app_handler.h"
#include "components/gcm_driver/gcm_client.h"
#include "components/gcm_driver/gcm_driver.h"
#include "components/gcm_driver/gcm_profile_service.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

using GoogleServicesBrowserTest = InProcessBrowserTest;

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

}  // namespace
}  // namespace ghost
