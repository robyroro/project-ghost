// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Pages at each protection level, on an embedded test server reached through
// host-resolver rules: the server records every request and its headers.

#include <map>
#include <memory>
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
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace ghost::privacy_policy {
namespace {

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

// A 1x1 transparent GIF.
constexpr char kGif[] = "GIF89a\x01\x00\x01\x00\x80\x00\x00\x00\x00\x00\xff\xff\xff\x21\xf9"
                        "\x04\x01\x00\x00\x00\x00\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00"
                        "\x02\x02\x44\x01\x00\x3b";

std::unique_ptr<HttpResponse> Serve(const HttpRequest& request) {
  auto response = std::make_unique<BasicHttpResponse>();
  const std::string path(request.GetURL().path());
  if (path.ends_with(".js")) {
    response->set_content_type("text/javascript");
    response->set_content("window.loaded = true;");
  } else if (path.ends_with(".gif")) {
    response->set_content_type("image/gif");
    response->set_content(std::string(kGif, sizeof(kGif) - 1));
  } else {
    response->set_content_type("text/html");
    response->set_content("<html><head></head><body>page</body></html>");
  }
  return response;
}

class ProtectionLevelsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestMonitor(
        base::BindRepeating(&ProtectionLevelsBrowserTest::Record, base::Unretained(this)));
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_test_server()->Start());
    // Incognito's strict HTTPS-First (3C) warns before any HTTP page.
    embedded_https_test_server().SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    embedded_https_test_server().RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_https_test_server().Start());
    ASSERT_TRUE(blocking::BlockingService::GetIfStarted());
    // A third-party tracker, and a rule for any site's own.js, which only a
    // check of same-site requests applies.
    blocking::BlockingService::GetIfStarted()->SetListsForTesting(
        {"||tracker.test^$third-party\n/own.js\n"});
  }

  GURL Url(std::string_view host, std::string_view path) {
    return embedded_test_server()->GetURL(host, path);
  }

  content::WebContents* Tab(Browser* browser = nullptr) {
    return (browser ? browser : this->browser())->tab_strip_model()->GetActiveWebContents();
  }

  std::string Load(std::string_view element, const GURL& src, Browser* browser = nullptr) {
    return content::EvalJs(Tab(browser),
                           content::JsReplace(base::StrCat({"new Promise(done => {"
                                                            " const e = document.createElement('",
                                                            element,
                                                            "'); e.onload = () => done('loaded');"
                                                            " e.onerror = () => done('error');"
                                                            " e.src = $1;"
                                                            " document.body.appendChild(e); })"}),
                                              src))
        .ExtractString();
  }

  // A header of the last request for this host and path; "<none>" if the
  // request carried none, "<no request>" if there was none.
  std::string Header(std::string_view host, std::string_view path, const std::string& name) {
    base::AutoLock lock(lock_);
    auto request = requests_.find(base::StrCat({host, path}));
    if (request == requests_.end()) {
      return "<no request>";
    }
    auto header = request->second.find(name);
    return header == request->second.end() ? "<none>" : header->second;
  }

 private:
  void Record(const HttpRequest& request) {
    base::AutoLock lock(lock_);
    std::string host = request.headers.at("Host");
    host = host.substr(0, host.find(':'));
    requests_[base::StrCat({host, std::string(request.GetURL().path())})] = request.headers;
  }

  base::Lock lock_;
  std::map<std::string, HttpRequest::HeaderMap> requests_;
};

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, AnOffSiteLoadsTrackers) {
  SetLevel(GetProfile(), Url("a.test", "/"), ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(Load("script", Url("tracker.test", "/t.js")), "loaded");
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, StandardLeavesTheSitesOwnRequests) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(Load("script", Url("tracker.test", "/t.js")), "error");
  EXPECT_EQ(Load("script", Url("a.test", "/own.js")), "loaded");
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, StrictBlocksTheSitesOwnTrackers) {
  SetLevel(GetProfile(), Url("a.test", "/"), ProtectionLevel::kStrict);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(Load("script", Url("a.test", "/own.js")), "error");
  EXPECT_EQ(Load("script", Url("cdn.a.test", "/own.js")), "error");
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, StrictSendsNoCrossSiteReferrer) {
  SetLevel(GetProfile(), Url("a.test", "/"), ProtectionLevel::kStrict);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  ASSERT_EQ(Load("img", Url("b.test", "/cross.gif")), "loaded");
  EXPECT_EQ(Header("b.test", "/cross.gif", "Referer"), "<none>");
  ASSERT_EQ(Load("img", Url("a.test", "/same.gif")), "loaded");
  EXPECT_EQ(Header("a.test", "/same.gif", "Referer"), Url("a.test", "/page").spec());
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, StandardSendsTheOriginCrossSite) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  ASSERT_EQ(Load("img", Url("b.test", "/cross.gif")), "loaded");
  EXPECT_EQ(Header("b.test", "/cross.gif", "Referer"), Url("a.test", "/").spec());
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, IncognitoIsStrictByDefault) {
  Browser* incognito = CreateIncognitoBrowser();
  const GURL page = embedded_https_test_server().GetURL("a.test", "/page");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, page));
  // The control: the page is there and loads its own scripts.
  ASSERT_EQ(Load("script", page.Resolve("/fine.js"), incognito), "loaded");
  EXPECT_EQ(Load("script", page.Resolve("/own.js"), incognito), "error");
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, ALevelAppliesAtTheNextLoad) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  ASSERT_EQ(Load("script", Url("tracker.test", "/t.js")), "error");
  SetLevel(GetProfile(), Url("a.test", "/"), ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(Load("script", Url("tracker.test", "/t2.js")), "loaded");
}

// Navigations: the destination's level decides what is stripped (3B), and a
// Strict page's level drops the Referer of its cross-site navigations.

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, AnOffSiteKeepsItsParameters) {
  SetLevel(GetProfile(), Url("b.test", "/"), ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/land?fbclid=1")));
  EXPECT_EQ(Tab()->GetLastCommittedURL(), Url("b.test", "/land?fbclid=1"));
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, StrictStripsCampaignParametersInNormal) {
  SetLevel(GetProfile(), Url("b.test", "/"), ProtectionLevel::kStrict);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/land?utm_source=n&x=1")));
  EXPECT_EQ(Tab()->GetLastCommittedURL(), Url("b.test", "/land?x=1"));
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, AStrictPageNavigatesWithoutReferrer) {
  SetLevel(GetProfile(), Url("a.test", "/"), ProtectionLevel::kStrict);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  content::TestNavigationObserver observer(Tab());
  ASSERT_TRUE(content::ExecJs(
      Tab(), content::JsReplace("location.href = $1;", Url("b.test", "/land"))));
  observer.Wait();
  EXPECT_EQ(Header("b.test", "/land", "Referer"), "<none>");
  // What the destination's script sees, for the progress notes.
  LOG(INFO) << "document.referrer after a Strict page's navigation: '"
            << content::EvalJs(Tab(), "document.referrer").ExtractString() << "'";
}

IN_PROC_BROWSER_TEST_F(ProtectionLevelsBrowserTest, AStandardPageNavigatesWithItsOrigin) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  content::TestNavigationObserver observer(Tab());
  ASSERT_TRUE(content::ExecJs(
      Tab(), content::JsReplace("location.href = $1;", Url("b.test", "/land"))));
  observer.Wait();
  EXPECT_EQ(Header("b.test", "/land", "Referer"), Url("a.test", "/").spec());
}

}  // namespace
}  // namespace ghost::privacy_policy
