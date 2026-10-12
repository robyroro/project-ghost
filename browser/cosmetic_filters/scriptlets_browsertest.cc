// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Scriptlets through the browser, the renderer and the engine, with a test
// list (Phase 4B).

#include <string>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_test_base.h"
#include "ghost/browser/privacy_policy/site_levels.h"

namespace ghost::cosmetic_filters {
namespace {

class ScriptletsBrowserTest : public CosmeticFiltersTestBase {
 protected:
  std::string List() const override {
    return "a.test##+js(set, shadeValue, noopFunc)\n"
           "a.test##+js(set, shade.nested.value, true)\n"
           "off.test##+js(set, shadeValue, noopFunc)\n"
           "except.test##+js(set, shadeValue, noopFunc)\n"
           "except.test#@#+js()\n";
  }

  void AddPages() override {
    // What the page's first script sees.
    AddPage("/first.html",
            "<script>window.seen = typeof shadeValue;"
            "window.shade = {}; shade.nested = {value: 5};"
            "window.nested = shade.nested.value;</script>");
    AddPage("/csp.html", "<script nonce=\"n\">window.seen = typeof shadeValue;</script>",
            {{"Content-Security-Policy", "script-src 'nonce-n'"}});
    AddPage("/written.html",
            "<iframe id=\"f\"></iframe><script>"
            "const d = document.getElementById('f').contentDocument;"
            "d.open(); d.write('<script>parent.childSeen = typeof shadeValue;<\\/script>');"
            "d.close();</script>");
  }
};

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, TheFirstScriptAlreadySeesTheScriptlet) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.seen"), "function");
  // A chain the page builds later is caught as it does.
  EXPECT_EQ(content::EvalJs(Tab(), "window.nested"), true);
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, APageCspDoesntStopIt) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/csp.html")));
  // EvalJs isn't subject to the page's script-src either.
  EXPECT_EQ(content::EvalJs(Tab(), "window.seen"), "function");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, NothingIsLeftOnWindow) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "[typeof shadeSafe, typeof shadeChain, typeof setConstant]"
                                   ".join()"),
            "undefined,undefined,undefined");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, OffRunsNone) {
  privacy_policy::SetLevel(GetProfile(), Url("off.test", "/"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("off.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.seen"), "undefined");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, IncognitoRunsThem) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, HttpsUrl("a.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(incognito), "window.seen"), "function");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, AnExceptionTurnsThemOff) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("except.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.seen"), "undefined");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, AFrameItsParentWritesTakesTheParentsScriptlets) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/written.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.childSeen"), "function");
}

}  // namespace
}  // namespace ghost::cosmetic_filters
