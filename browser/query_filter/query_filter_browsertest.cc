// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Navigations through the query filter, on an embedded test server reached
// through host-resolver rules: no real network. The server records every
// request, so a test can show that a URL never left the browser.

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "ghost/browser/query_filter/prefs.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace ghost::query_filter {
namespace {

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

// /redirect?<url> redirects there; anything else is a page.
std::unique_ptr<HttpResponse> Serve(const HttpRequest& request) {
  auto response = std::make_unique<BasicHttpResponse>();
  const GURL url = request.GetURL();
  if (url.path() == "/redirect") {
    response->set_code(net::HTTP_FOUND);
    response->AddCustomHeader("Location", std::string(url.query()));
  } else {
    response->set_content_type("text/html");
    response->set_content("<html><head></head><body>page</body></html>");
  }
  return response;
}

class QueryFilterBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestMonitor(
        base::BindRepeating(&QueryFilterBrowserTest::Record, base::Unretained(this)));
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  GURL Url(std::string_view host, std::string_view path_and_query) {
    return embedded_test_server()->GetURL(host, path_and_query);
  }

  // Whether the server received this host and path with this query.
  bool Received(std::string_view host, std::string_view path_and_query) {
    base::AutoLock lock(lock_);
    return seen_.contains(base::StrCat({host, path_and_query}));
  }

  content::WebContents* Tab(Browser* browser = nullptr) {
    return (browser ? browser : this->browser())->tab_strip_model()->GetActiveWebContents();
  }

  // The URL the tab committed after a browser-initiated navigation (the
  // address bar, a bookmark: no initiator).
  GURL NavigateFromBrowser(const GURL& url, Browser* browser = nullptr) {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser ? browser : this->browser(), url));
    return Tab(browser)->GetLastCommittedURL();
  }

  // The same, started by the page in the tab (a link, location.href).
  GURL NavigateFromPage(const GURL& url) {
    content::TestNavigationObserver observer(Tab());
    EXPECT_TRUE(content::ExecJs(Tab(), content::JsReplace("location.href = $1;", url)));
    observer.Wait();
    return Tab()->GetLastCommittedURL();
  }

 private:
  void Record(const HttpRequest& request) {
    base::AutoLock lock(lock_);
    std::string host = request.headers.at("Host");
    host = host.substr(0, host.find(':'));
    seen_.insert(base::StrCat({host, request.relative_url}));
  }

  base::Lock lock_;
  std::set<std::string> seen_;
};

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, ALinkFromAnotherSiteArrivesClean) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  content::NavigationController& controller = Tab()->GetController();
  const int entries = controller.GetEntryCount();

  EXPECT_EQ(NavigateFromPage(Url("b.test", "/land?fbclid=1&x=2")),
            Url("b.test", "/land?x=2"));
  EXPECT_TRUE(Received("b.test", "/land?x=2"));
  EXPECT_FALSE(Received("b.test", "/land?fbclid=1&x=2")) << "the original URL left the browser";
  EXPECT_EQ(controller.GetEntryCount(), entries + 1);
  EXPECT_EQ(controller.GetLastCommittedEntry()->GetURL(), Url("b.test", "/land?x=2"));
}

// The history entry keeps the URL the navigation started with as its
// "original request URL" (in the session file on disk). Loading it again, as
// "request desktop site" does, is a navigation without an initiator: it is
// stripped again, and the original still never leaves the browser.
IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, ReloadingTheOriginalRequestURLSendsItClean) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  NavigateFromPage(Url("b.test", "/land?fbclid=1&x=2"));
  content::WebContents* tab = Tab();
  tab->GetController().LoadOriginalRequestURL();
  EXPECT_TRUE(content::WaitForLoadStop(tab));
  EXPECT_EQ(tab->GetLastCommittedURL(), Url("b.test", "/land?x=2"));
  EXPECT_FALSE(Received("b.test", "/land?fbclid=1&x=2"));
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, ALinkWithinASiteKeepsItsParameters) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(NavigateFromPage(Url("www.a.test", "/land?fbclid=1")),
            Url("www.a.test", "/land?fbclid=1"));
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, ACrossSiteRedirectArrivesClean) {
  const GURL target = Url("b.test", "/land?gclid=1&x=2");
  EXPECT_EQ(NavigateFromBrowser(Url("r.test", base::StrCat({"/redirect?", target.spec()}))),
            Url("b.test", "/land?x=2"));
  EXPECT_TRUE(Received("b.test", "/land?x=2"));
  EXPECT_FALSE(Received("b.test", "/land?gclid=1&x=2"));
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, ASameSiteRedirectKeepsItsParameters) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  const GURL target = Url("www.a.test", "/land?gclid=1");
  EXPECT_EQ(NavigateFromPage(Url("a.test", base::StrCat({"/redirect?", target.spec()}))),
            target);
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, TheAddressBarArrivesClean) {
  EXPECT_EQ(NavigateFromBrowser(Url("b.test", "/land?fbclid=1")), Url("b.test", "/land"));
  EXPECT_FALSE(Received("b.test", "/land?fbclid=1"));
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, HistoryHoldsOnlyTheCleanURL) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  NavigateFromPage(Url("b.test", "/land?fbclid=1&x=2"));
  NavigateFromBrowser(Url("c.test", "/land?gclid=1"));
  ui_test_utils::HistoryEnumerator history(browser()->GetProfile());
  for (const GURL& url : history.urls()) {
    EXPECT_EQ(url.spec().find("clid"), std::string::npos) << url;
  }
  EXPECT_TRUE(std::ranges::contains(history.urls(), Url("b.test", "/land?x=2")));
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, CampaignParametersStayInNormal) {
  EXPECT_EQ(NavigateFromBrowser(Url("b.test", "/land?utm_source=n&fbclid=1")),
            Url("b.test", "/land?utm_source=n"));
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, CampaignParametersAreStrippedInIncognito) {
  Browser* incognito = CreateIncognitoBrowser();
  EXPECT_EQ(NavigateFromBrowser(Url("b.test", "/land?utm_source=n&fbclid=1"), incognito),
            Url("b.test", "/land"));
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, CampaignParametersAreStrippedWithThePref) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(kStripCampaignParametersPref, true);
  EXPECT_EQ(NavigateFromBrowser(Url("b.test", "/land?utm_source=n&fbclid=1")),
            Url("b.test", "/land"));
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, AnIframeKeepsItsParameters) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  content::TestNavigationObserver observer(Tab());
  ASSERT_TRUE(content::ExecJs(
      Tab(), content::JsReplace("const f = document.createElement('iframe');"
                                "f.src = $1; document.body.appendChild(f);",
                                Url("b.test", "/frame?fbclid=1"))));
  observer.Wait();
  EXPECT_TRUE(Received("b.test", "/frame?fbclid=1"));
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, AFormPostKeepsItsParameters) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  content::TestNavigationObserver observer(Tab());
  ASSERT_TRUE(content::ExecJs(
      Tab(), content::JsReplace("const f = document.createElement('form');"
                                "f.method = 'post'; f.action = $1;"
                                "document.body.appendChild(f); f.submit();",
                                Url("b.test", "/form?fbclid=1"))));
  observer.Wait();
  EXPECT_TRUE(Received("b.test", "/form?fbclid=1"));
  EXPECT_EQ(Tab()->GetLastCommittedURL(), Url("b.test", "/form?fbclid=1"));
}

IN_PROC_BROWSER_TEST_F(QueryFilterBrowserTest, BackForwardAndReloadUseTheCleanURL) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  NavigateFromPage(Url("b.test", "/land?fbclid=1&x=2"));
  NavigateFromBrowser(Url("c.test", "/other"));
  content::WebContents* tab = Tab();
  ASSERT_TRUE(content::HistoryGoBack(tab));
  EXPECT_EQ(tab->GetLastCommittedURL(), Url("b.test", "/land?x=2"));
  tab->GetController().Reload(content::ReloadType::BYPASSING_CACHE, false);
  EXPECT_TRUE(content::WaitForLoadStop(tab));
  EXPECT_EQ(tab->GetLastCommittedURL(), Url("b.test", "/land?x=2"));
  EXPECT_FALSE(Received("b.test", "/land?fbclid=1&x=2"));
}

}  // namespace
}  // namespace ghost::query_filter
