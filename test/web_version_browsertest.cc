// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// ADR 0007: websites see the Chromium release; people see the release, as
// "<Chromium release>-<respin>".

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/version.h"
#include "chrome/browser/ui/webui/version/version_ui.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/version/version.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost {
namespace {

using VersionDisplayBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(VersionDisplayBrowserTest, AboutShowsTheDisplayVersion) {
  const std::u16string about = VersionUI::GetAnnotatedVersionStringForUi();
  EXPECT_NE(about.find(base::UTF8ToUTF16(DisplayVersion() + " ")),
            std::u16string::npos)
      << about;
}

IN_PROC_BROWSER_TEST_F(VersionDisplayBrowserTest,
                       ChromeVersionShowsTheDisplayVersion) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version")));
  const std::string shown =
      content::EvalJs(chrome_test_utils::GetActiveWebContents(this),
                      "document.getElementById('version').innerText")
          .ExtractString();
  EXPECT_EQ(shown.rfind(DisplayVersion() + " ", 0), 0u) << shown;
}

}  // namespace
}  // namespace ghost
