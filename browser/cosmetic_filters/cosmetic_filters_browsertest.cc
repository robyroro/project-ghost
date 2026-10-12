// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Cosmetic filtering through the browser, the renderer and the engine, with a
// test list, on an embedded test server reached through host-resolver rules.

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace ghost::cosmetic_filters {
namespace {

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

constexpr char kTestList[] =
    "a.test##.site-ad\n"
    "c.test##.frame-ad\n"
    "nohide.test##.site-ad\n"
    "##.generic-ad\n"
    "###banner\n"
    "##.allowed-ad\n"
    "a.test#@#.allowed-ad\n"
    "@@||nohide.test^$generichide\n";

// The pages, by path.
const std::map<std::string, std::string>& Pages() {
  static const base::NoDestructor<std::map<std::string, std::string>> pages({
      {"/site.html",
       "<div class=\"site-ad\" id=\"site\">ad</div>"
       "<script>window.seenAtParse ="
       " getComputedStyle(document.getElementById('site')).display;</script>"},
      {"/generic.html",
       "<div class=\"generic-ad\" id=\"generic\">ad</div>"
       "<div class=\"x\" id=\"banner\">ad</div>"
       "<div class=\"allowed-ad\" id=\"allowed\">ad</div>"
       "<div class=\"site-ad\" id=\"site\">ad</div>"
       "<div class=\"plain\" id=\"plain\">text</div>"},
      {"/override.html",
       "<style>.site-ad{display:block !important}</style>"
       "<div class=\"site-ad\" id=\"site\">ad</div>"},
      {"/framed.html",
       "<div class=\"frame-ad\" id=\"frame-ad\">ad</div>"
       "<div class=\"site-ad\" id=\"site\">ad</div>"},
      {"/written.html",
       "<iframe id=\"f\"></iframe><script>"
       "document.getElementById('f').contentDocument.body.innerHTML ="
       " '<div class=\"site-ad\" id=\"inner\">ad</div>"
       "<div class=\"generic-ad\" id=\"inner-generic\">ad</div>';</script>"},
      {"/page.html", "<p>page</p>"},
  });
  return *pages;
}

std::unique_ptr<HttpResponse> Serve(const HttpRequest& request) {
  auto response = std::make_unique<BasicHttpResponse>();
  const GURL url = request.GetURL();
  const std::string path(url.path() == "/csp.html" ? "/generic.html" : url.path());
  auto page = Pages().find(path);
  if (page == Pages().end()) {
    response->set_code(net::HTTP_NOT_FOUND);
    return response;
  }
  if (url.path() == "/csp.html") {
    response->AddCustomHeader("Content-Security-Policy", "script-src 'none'");
  }
  response->set_content_type("text/html");
  response->set_content("<!doctype html><html><body>" + page->second + "</body></html>");
  return response;
}

class CosmeticFiltersBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_test_server()->Start());
    // Incognito's strict HTTPS-First (3C) warns before any HTTP page.
    embedded_https_test_server().SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    embedded_https_test_server().RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_https_test_server().Start());
    ASSERT_TRUE(blocking::BlockingService::GetIfStarted());
    blocking::BlockingService::GetIfStarted()->SetListsForTesting({kTestList});
  }

  GURL Url(std::string_view host, std::string_view path) {
    return embedded_test_server()->GetURL(host, path);
  }

  content::WebContents* Tab(Browser* browser = nullptr) {
    return (browser ? browser : this->browser())->tab_strip_model()->GetActiveWebContents();
  }

  // The element's computed display, at once.
  std::string Display(const content::ToRenderFrameHost& frame, std::string_view id) {
    return content::EvalJs(frame, content::JsReplace(
                                      "getComputedStyle(document.getElementById($1)).display", id))
        .ExtractString();
  }

  // The display of the element `script` finds, once it is `expected`, or
  // after 2 s: generic rules arrive after a round trip to the browser. The
  // script is pasted in, not eval'd: a page's CSP may forbid eval.
  std::string WaitForDisplayOf(const content::ToRenderFrameHost& frame,
                               std::string_view script,
                               std::string_view expected) {
    return content::EvalJs(frame, base::StrCat({R"(new Promise(done => {
               const start = performance.now();
               (function poll() {
                 const element = )",
                                                script, R"(;
                 const shown = element ? getComputedStyle(element).display : 'missing';
                 if (shown === )",
                                                content::JsReplace("$1", expected), R"( ||
                     performance.now() - start > 2000) {
                   done(shown);
                 } else {
                   requestAnimationFrame(poll);
                 }
               })();
             }))"}))
        .ExtractString();
  }

  std::string WaitForDisplay(const content::ToRenderFrameHost& frame,
                             std::string_view id,
                             std::string_view expected) {
    return WaitForDisplayOf(frame, content::JsReplace("document.getElementById($1)", id),
                            expected);
  }

  // Waits as long as a scanner would take to answer (20 times its batch
  // interval), for checks that something stays shown.
  void WaitForAScanner(content::WebContents* tab) {
    ASSERT_TRUE(content::ExecJs(tab, "new Promise(done => setTimeout(done, 2000))"));
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
      incognito, embedded_https_test_server().GetURL("a.test", "/site.html")));
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
