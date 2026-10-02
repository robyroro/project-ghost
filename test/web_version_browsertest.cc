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
#include "net/test/embedded_test_server/embedded_test_server.h"
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

using WebVersionBrowserTest = InProcessBrowserTest;

// Everything a page can ask about the browser's version names the Chromium
// release. When this build's release version differs (a release, or the
// spike), the page never sees it.
IN_PROC_BROWSER_TEST_F(WebVersionBrowserTest, PagesSeeTheChromiumRelease) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  content::WebContents* contents =
      chrome_test_utils::GetActiveWebContents(this);
  const std::string seen =
      content::EvalJs(contents, R"(
        navigator.userAgentData
            .getHighEntropyValues(["fullVersionList", "uaFullVersion"])
            .then(v => navigator.userAgent + " " + JSON.stringify(v)))")
          .ExtractString();

  const std::string chromium = ChromiumVersion().GetString();
  EXPECT_NE(seen.find("\"uaFullVersion\":\"" + chromium + "\""),
            std::string::npos)
      << seen;
  EXPECT_NE(seen.find("\"version\":\"" + chromium + "\""), std::string::npos)
      << seen;

  const std::string release = ReleaseVersion().GetString();
  if (release != chromium) {
    EXPECT_EQ(seen.find(release), std::string::npos) << seen;
  }
}

}  // namespace
}  // namespace ghost
