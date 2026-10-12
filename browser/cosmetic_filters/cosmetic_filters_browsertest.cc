// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Cosmetic filtering through the browser, the renderer and the engine, with a
// test list, on an embedded test server reached through host-resolver rules.

#include <string>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_test_base.h"
#include "ghost/browser/privacy_policy/site_levels.h"

namespace ghost::cosmetic_filters {
namespace {

constexpr char kGenericPage[] =
    "<div class=\"generic-ad\" id=\"generic\">ad</div>"
    "<div class=\"x\" id=\"banner\">ad</div>"
    "<div class=\"allowed-ad\" id=\"allowed\">ad</div>"
    "<div class=\"site-ad\" id=\"site\">ad</div>"
    "<div class=\"plain\" id=\"plain\">text</div>";

class CosmeticFiltersBrowserTest : public CosmeticFiltersTestBase {
 protected:
  std::string List() const override {
    return "a.test##.site-ad\n"
           "c.test##.frame-ad\n"
           "nohide.test##.site-ad\n"
           "##.generic-ad\n"
           "###banner\n"
           "##.allowed-ad\n"
           "a.test#@#.allowed-ad\n"
           "@@||nohide.test^$generichide\n";
  }

  void AddPages() override {
    AddPage("/site.html",
            "<div class=\"site-ad\" id=\"site\">ad</div>"
            "<script>window.seenAtParse ="
            " getComputedStyle(document.getElementById('site')).display;</script>");
    AddPage("/generic.html", kGenericPage);
    AddPage("/csp.html", kGenericPage, {{"Content-Security-Policy", "script-src 'none'"}});
    AddPage("/override.html",
            "<style>.site-ad{display:block !important}</style>"
            "<div class=\"site-ad\" id=\"site\">ad</div>");
    AddPage("/framed.html",
            "<div class=\"frame-ad\" id=\"frame-ad\">ad</div>"
            "<div class=\"site-ad\" id=\"site\">ad</div>");
    AddPage("/written.html",
            "<iframe id=\"f\"></iframe><script>"
            "document.getElementById('f').contentDocument.body.innerHTML ="
            " '<div class=\"site-ad\" id=\"inner\">ad</div>"
            "<div class=\"generic-ad\" id=\"inner-generic\">ad</div>';</script>");
    AddPage("/page.html", "<p>page</p>");
  }
};

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, SiteRuleHidesBeforeThePageSeesIt) {
  // Alternating sites swaps renderer processes: the rules must still come first.
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/site.html")));
    EXPECT_EQ(content::EvalJs(Tab(), "window.seenAtParse"), "none");
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/site.html")));
    EXPECT_EQ(content::EvalJs(Tab(), "window.seenAtParse"), "block");
  }
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, GenericRulesHideByClassAndId) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/generic.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "generic", "none"), "none");
  EXPECT_EQ(WaitForDisplay(Tab(), "banner", "none"), "none");
  EXPECT_EQ(Display(Tab(), "plain"), "block");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, GenericRulesHideElementsAddedLater) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/page.html")));
  ASSERT_TRUE(content::ExecJs(Tab(), R"(
      document.body.insertAdjacentHTML('beforeend',
          '<div class="generic-ad" id="late">ad</div><div class="other" id="late-plain">x</div>');
  )"));
  EXPECT_EQ(WaitForDisplay(Tab(), "late", "none"), "none");
  // Both names went in the same batch, so this one has been answered too.
  EXPECT_EQ(Display(Tab(), "late-plain"), "block");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, AnExceptionShowsTheElementOnItsSite) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/generic.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "generic", "none"), "none");
  EXPECT_EQ(Display(Tab(), "allowed"), "block");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/generic.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "allowed", "none"), "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, GenerichideTurnsGenericRulesOff) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("nohide.test", "/generic.html")));
  // The site's own rule applies...
  EXPECT_EQ(Display(Tab(), "site"), "none");
  // ...the generic ones never do: no scanner runs.
  WaitForAScanner(Tab());
  EXPECT_EQ(Display(Tab(), "generic"), "block");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, OffHidesNothing) {
  privacy_policy::SetLevel(GetProfile(), Url("a.test", "/"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/site.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.seenAtParse"), "block");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/generic.html")));
  WaitForAScanner(Tab());
  EXPECT_EQ(Display(Tab(), "generic"), "block");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, IncognitoHides) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      incognito, HttpsUrl("a.test", "/site.html")));
  EXPECT_EQ(content::EvalJs(Tab(incognito), "window.seenAtParse"), "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, ACrossSiteFrameHasItsOwnRules) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page.html")));
  content::TestNavigationObserver observer(Tab());
  ASSERT_TRUE(content::ExecJs(
      Tab(), content::JsReplace("const f = document.createElement('iframe');"
                                "f.src = $1; document.body.appendChild(f);",
                                Url("c.test", "/framed.html"))));
  observer.Wait();
  content::RenderFrameHost* frame = content::ChildFrameAt(Tab(), 0);
  ASSERT_TRUE(frame);
  EXPECT_EQ(Display(frame, "frame-ad"), "none");
  // a.test's rule is the page's site's, not the frame's.
  EXPECT_EQ(Display(frame, "site"), "block");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, AFrameItsParentWritesTakesTheParentsRules) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/written.html")));
  EXPECT_EQ(WaitForDisplayOf(Tab(),
                             "document.getElementById('f').contentDocument"
                             ".getElementById('inner')",
                             "none"),
            "none");
  // Its scanner runs too.
  EXPECT_EQ(WaitForDisplayOf(Tab(),
                             "document.getElementById('f').contentDocument"
                             ".getElementById('inner-generic')",
                             "none"),
            "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, ThePageSeesNothingOfUs) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/override.html")));
  // A user !important declaration beats the page's.
  EXPECT_EQ(Display(Tab(), "site"), "none");
  EXPECT_EQ(content::EvalJs(Tab(), "typeof shadeCosmeticFilters"), "undefined");
  // Only the page's own <style>.
  EXPECT_EQ(content::EvalJs(Tab(), "document.styleSheets.length"), 1);
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, AStrictCspDoesntStopTheScanner) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/csp.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "generic", "none"), "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, ALongClassNameDoesntEndTheRenderer) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/page.html")));
  ASSERT_TRUE(content::ExecJs(Tab(), R"(
      const d = document.createElement('div');
      d.id = 'long';
      d.className = 'x'.repeat(300) + ' generic-ad';
      document.body.appendChild(d);
  )"));
  EXPECT_EQ(WaitForDisplay(Tab(), "long", "none"), "none");
  EXPECT_FALSE(Tab()->IsCrashed());
}

}  // namespace
}  // namespace ghost::cosmetic_filters
