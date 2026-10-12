// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Cosmetic filtering through the browser and the renderer, on an embedded
// test server reached through host-resolver rules. Spike: fixed rules hide
// .shade-spike (site-specific) and .shade-spike-generic (generic).

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace ghost::cosmetic_filters {
namespace {

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

// The pages, by path.
const std::map<std::string, std::string>& Pages() {
  static const base::NoDestructor<std::map<std::string, std::string>> pages({
      {"/site.html",
       "<div class=\"shade-spike\" id=\"x\">ad</div>"
       "<script>window.seen = getComputedStyle(document.getElementById('x')).display;</script>"},
      {"/generic.html", "<div class=\"shade-spike-generic\" id=\"g\">ad</div>"},
      {"/written.html",
       "<iframe id=\"f\"></iframe><script>"
       "document.getElementById('f').contentDocument.body.innerHTML ="
       " '<div class=\"shade-spike\" id=\"i\">ad</div>';</script>"},
      {"/srcdoc.html",
       "<iframe id=\"f\" srcdoc=\"<div class='shade-spike' id='i'>ad</div>\"></iframe>"},
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
  }

  GURL Url(std::string_view host, std::string_view path) {
    return embedded_test_server()->GetURL(host, path);
  }

  content::WebContents* Tab() { return browser()->tab_strip_model()->GetActiveWebContents(); }

  // The element's computed display once it is `expected`, or after 2 s.
  std::string WaitForDisplay(const content::ToRenderFrameHost& frame,
                             std::string_view script_for_element,
                             std::string_view expected) {
    return content::EvalJs(frame, base::StrCat({R"(new Promise(done => {
               const start = performance.now();
               (function poll() {
                 const element = )",
                                                script_for_element, R"(;
                 const shown = element ? getComputedStyle(element).display : 'missing';
                 if (shown === ')",
                                                expected, R"(' || performance.now() - start > 2000) {
                   done(shown);
                 } else {
                   requestAnimationFrame(poll);
                 }
               })();
             }))"}))
        .ExtractString();
  }
};

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, SpikeSiteSheetBeforeParse) {
  // Alternating sites swaps renderer processes.
  for (int i = 0; i < 30; ++i) {
    ASSERT_TRUE(
        ui_test_utils::NavigateToURL(browser(), Url(i % 2 ? "b.test" : "a.test", "/site.html")));
    EXPECT_EQ(content::EvalJs(Tab(), "window.seen"), "none") << "navigation " << i;
  }
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, SpikeGeneric) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page.html")));
  ASSERT_TRUE(content::ExecJs(
      Tab(), "document.body.insertAdjacentHTML('beforeend',"
             " '<div class=\"shade-spike-generic\" id=\"g\">ad</div>');"));
  EXPECT_EQ(WaitForDisplay(Tab(), "document.getElementById('g')", "none"), "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, SpikeWrittenFrame) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/written.html")));
  EXPECT_EQ(WaitForDisplay(
                Tab(), "document.getElementById('f').contentDocument.getElementById('i')", "none"),
            "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, SpikeSrcdocFrame) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/srcdoc.html")));
  EXPECT_EQ(WaitForDisplay(
                Tab(), "document.getElementById('f').contentDocument.getElementById('i')", "none"),
            "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, SpikeCsp) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/csp.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "document.getElementById('g')", "none"), "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, SpikePageCantSeeUs) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/generic.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "document.getElementById('g')", "none"), "none");
  EXPECT_EQ(content::EvalJs(Tab(), "typeof shadeCosmeticFilters"), "undefined");
  EXPECT_EQ(content::EvalJs(Tab(), "document.styleSheets.length"), 0);
}

}  // namespace
}  // namespace ghost::cosmetic_filters
