// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Requests through the blocking engine, on an embedded test server reached
// through host-resolver rules: no real network.

#include <set>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/components/blocking/filter_lists.h"
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace ghost::blocking {
namespace {

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

// A 1x1 transparent GIF.
constexpr char kGif[] = "GIF89a\x01\x00\x01\x00\x80\x00\x00\x00\x00\x00\xff\xff\xff\x21\xf9"
                        "\x04\x01\x00\x00\x00\x00\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00"
                        "\x02\x02\x44\x01\x00\x3b";

// Serves any path: /redirect?<url> redirects, *.js is a script, *.gif an
// image, anything else a page.
std::unique_ptr<HttpResponse> Serve(const HttpRequest& request) {
  auto response = std::make_unique<BasicHttpResponse>();
  const GURL url = request.GetURL();
  if (url.path() == "/redirect") {
    response->set_code(net::HTTP_FOUND);
    response->AddCustomHeader("Location", std::string(url.query()));
  } else if (url.path().ends_with(".js")) {
    response->set_content_type("text/javascript");
    response->set_content("window.loaded = true;");
  } else if (url.path().ends_with(".gif")) {
    response->set_content_type("image/gif");
    response->set_content(std::string(kGif, sizeof(kGif) - 1));
  } else {
    response->set_content_type("text/html");
    response->set_content("<html><head></head><body>page</body></html>");
  }
  return response;
}

class RequestFilterBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestMonitor(
        base::BindRepeating(&RequestFilterBrowserTest::Record, base::Unretained(this)));
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(BlockingService::GetIfStarted());
    BlockingService::GetIfStarted()->SetListsForTesting({"||tracker.test^$third-party\n"});
  }

  GURL Url(std::string_view host, std::string_view path) {
    return embedded_test_server()->GetURL(host, path);
  }

  // Whether the server saw the request: a blocked one never leaves the browser.
  bool Reached(std::string_view host, std::string_view path) {
    base::AutoLock lock(lock_);
    return seen_.contains(base::StrCat({host, path}));
  }

  content::WebContents* Tab(Browser* browser = nullptr) {
    return (browser ? browser : this->browser())->tab_strip_model()->GetActiveWebContents();
  }

  std::string LoadScript(content::WebContents* tab, const GURL& src) {
    return content::EvalJs(tab, content::JsReplace(R"(new Promise(done => {
               const s = document.createElement('script');
               s.src = $1;
               s.onload = () => done('loaded');
               s.onerror = () => done('error');
               document.head.appendChild(s);
             }))",
                                                   src))
        .ExtractString();
  }

  std::string LoadImage(content::WebContents* tab, const GURL& src) {
    return content::EvalJs(tab, content::JsReplace(R"(new Promise(done => {
               const i = new Image();
               i.onload = () => done('loaded');
               i.onerror = () => done('error');
               i.src = $1;
             }))",
                                                   src))
        .ExtractString();
  }

 private:
  void Record(const HttpRequest& request) {
    base::AutoLock lock(lock_);
    const GURL url = request.GetURL();
    // The Host header names the host the page asked for.
    std::string host = request.headers.at("Host");
    host = host.substr(0, host.find(':'));
    seen_.insert(base::StrCat({host, url.path()}));
  }

  base::Lock lock_;
  std::set<std::string> seen_;
};

IN_PROC_BROWSER_TEST_F(RequestFilterBrowserTest, BlocksAThirdPartyTrackersScript) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page.html")));
  EXPECT_EQ(LoadScript(Tab(), Url("tracker.test", "/t.js")), "error");
  EXPECT_FALSE(Reached("tracker.test", "/t.js"));
}

IN_PROC_BROWSER_TEST_F(RequestFilterBrowserTest, ABlockedFrameFailsWithBlockedByClient) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page.html")));
  content::TestNavigationObserver observer(Tab());
  ASSERT_TRUE(content::ExecJs(
      Tab(), content::JsReplace("const f = document.createElement('iframe');"
                                "f.src = $1; document.body.appendChild(f);",
                                Url("tracker.test", "/frame.html"))));
  observer.Wait();
  EXPECT_EQ(observer.last_net_error_code(), net::ERR_BLOCKED_BY_CLIENT);
  EXPECT_FALSE(Reached("tracker.test", "/frame.html"));
}

IN_PROC_BROWSER_TEST_F(RequestFilterBrowserTest, TheSameSiteIsNotChecked) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page.html")));
  EXPECT_EQ(LoadScript(Tab(), Url("a.test", "/own.js")), "loaded");
  EXPECT_EQ(LoadScript(Tab(), Url("cdn.a.test", "/own.js")), "loaded");
}

IN_PROC_BROWSER_TEST_F(RequestFilterBrowserTest, ARedirectToATrackerIsBlocked) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page.html")));
  const GURL tracker = Url("tracker.test", "/p.gif");
  EXPECT_EQ(LoadImage(Tab(), Url("a.test", base::StrCat({"/redirect?", tracker.spec()}))),
            "error");
  EXPECT_TRUE(Reached("a.test", "/redirect"));
  EXPECT_FALSE(Reached("tracker.test", "/p.gif"));
}

IN_PROC_BROWSER_TEST_F(RequestFilterBrowserTest, ATopLevelNavigationAlwaysLoads) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("tracker.test", "/page.html")));
  EXPECT_TRUE(Reached("tracker.test", "/page.html"));
}

IN_PROC_BROWSER_TEST_F(RequestFilterBrowserTest, IncognitoIsFilteredToo) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, Url("a.test", "/page.html")));
  EXPECT_EQ(LoadScript(Tab(incognito), Url("tracker.test", "/t.js")), "error");
  EXPECT_FALSE(Reached("tracker.test", "/t.js"));
}

IN_PROC_BROWSER_TEST_F(RequestFilterBrowserTest, TheShippedListsBlockKnownAdsAndTrackers) {
  // EasyList: `||adnxs.com^`. EasyPrivacy: `||taboola.com^$third-party`.
  const GURL ad = Url("ib.adnxs.com", "/ads.js");
  const GURL tracker = Url("cdn.taboola.com", "/t.js");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page.html")));
  // The control: with the test list only, both load, so nothing else (HSTS,
  // say) stands in their way.
  ASSERT_EQ(LoadScript(Tab(), ad), "loaded");
  ASSERT_EQ(LoadScript(Tab(), tracker), "loaded");
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    BlockingService::GetIfStarted()->SetListsForTesting(
        ReadFilterLists(DefaultFilterListsDir()));
  }
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/page.html")));
  EXPECT_EQ(LoadScript(Tab(), Url("ib.adnxs.com", "/more-ads.js")), "error");
  EXPECT_EQ(LoadScript(Tab(), Url("cdn.taboola.com", "/more.js")), "error");
  EXPECT_EQ(LoadScript(Tab(), Url("cdn.b.test", "/own.js")), "loaded");
  EXPECT_FALSE(Reached("ib.adnxs.com", "/more-ads.js"));
  EXPECT_FALSE(Reached("cdn.taboola.com", "/more.js"));
}

}  // namespace
}  // namespace ghost::blocking
