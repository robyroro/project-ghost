// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/settings/protections_handler.h"

#include <memory>
#include <string>

#include "base/values.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/test_web_ui.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::settings {
namespace {

class ProtectionsHandlerTest : public ChromeRenderViewHostTestHarness {
 protected:
  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    web_ui_.set_web_contents(web_contents());
    auto handler = std::make_unique<ProtectionsHandler>(profile());
    ProtectionsHandler* raw = handler.get();
    web_ui_.AddMessageHandler(std::move(handler));
    raw->AllowJavascriptForTesting();
  }

  void Send(const std::string& message, base::ListValue args) {
    web_ui_.HandleReceivedMessage(message, args);
  }

  const base::DictValue& Levels() {
    return profile()->GetPrefs()->GetDict(privacy_policy::kSiteLevelsPref);
  }

  content::TestWebUI web_ui_;
};

TEST_F(ProtectionsHandlerTest, SetsASitesLevel) {
  Send("shadeSetSiteLevel", base::ListValue().Append("news.test").Append("strict"));
  ASSERT_TRUE(Levels().FindString("news.test"));
  EXPECT_EQ(*Levels().FindString("news.test"), "strict");
}

TEST_F(ProtectionsHandlerTest, KeysByRegistrableDomain) {
  Send("shadeSetSiteLevel", base::ListValue().Append("www.news.test").Append("off"));
  EXPECT_TRUE(Levels().contains("news.test"));
  EXPECT_FALSE(Levels().contains("www.news.test"));
}

TEST_F(ProtectionsHandlerTest, RefusesAnUnknownLevelOrNoSite) {
  Send("shadeSetSiteLevel", base::ListValue().Append("news.test").Append("strictest"));
  Send("shadeSetSiteLevel", base::ListValue().Append("").Append("off"));
  Send("shadeSetSiteLevel", base::ListValue().Append("a b").Append("off"));
  Send("shadeSetSiteLevel", base::ListValue().Append("news.test/path").Append("off"));
  Send("shadeSetSiteLevel", base::ListValue().Append(1).Append("off"));
  Send("shadeSetSiteLevel", base::ListValue().Append("news.test"));
  EXPECT_TRUE(Levels().empty());
}

TEST_F(ProtectionsHandlerTest, ClearsASite) {
  Send("shadeSetSiteLevel", base::ListValue().Append("news.test").Append("off"));
  Send("shadeClearSiteLevel", base::ListValue().Append("news.test"));
  EXPECT_FALSE(Levels().contains("news.test"));
}

}  // namespace
}  // namespace ghost::settings
