// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// CNAME uncloaking (Phase 4C): the request filter resolves a request's host
// and blocks it when one of its DNS aliases is listed, before it leaves.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/browser/blocking/alias_resolver.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_test_base.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/protections/page_protections.h"
#include "ghost/components/privacy_policy/protection_level.h"
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace ghost::blocking {
namespace {

class CnameUncloakingBrowserTest : public cosmetic_filters::CosmeticFiltersTestBase {
 protected:
  // `^` matches the port the test server's URLs carry.
  std::string List() const override {
    return "||tracker.test^\n"
           "@@||allowed.a.test^\n"
           "||edge.a.test^\n"
           "||blocked.test^\n"
           "||widgets.test^$script,redirect=noopjs\n";
  }

  void AddHostRules() override {
    // The rules match by host name; each host has one alias unless the test
    // says otherwise.
    host_resolver()->AddIPLiteralRuleWithDnsAliases("metrics.a.test", "127.0.0.1",
                                                    {"a.tracker.test"});
    host_resolver()->AddIPLiteralRuleWithDnsAliases("stats.b.test", "127.0.0.1",
                                                    {"b.tracker.test"});
    host_resolver()->AddIPLiteralRuleWithDnsAliases("allowed.a.test", "127.0.0.1",
                                                    {"c.tracker.test"});
    host_resolver()->AddIPLiteralRuleWithDnsAliases("cdn.a.test", "127.0.0.1",
                                                    {"edge.a.test"});
    host_resolver()->AddIPLiteralRuleWithDnsAliases("widget.a.test", "127.0.0.1",
                                                    {"cdn.widgets.test"});
    host_resolver()->AddIPLiteralRuleWithDnsAliases("chain.a.test", "127.0.0.1",
                                                    {"edge.cdn.test", "d.tracker.test"});
    host_resolver()->AddSimulatedFailure("gone.a.test");
  }

  void AddPages() override {
    AddPage("/page.html", "<p>page</p>");
    for (const char* path : {"/metrics.js", "/stats.js", "/allowed.js", "/edge.js",
                             "/hop-target.js", "/blocked.js", "/widget.js", "/chain.js",
                             "/gone.js", "/proxy.js"}) {
      AddResource(path, "application/javascript", "window.loaded = true;");
    }
    AddRedirect("/hop.js", "metrics.a.test", "/hop-target.js");
  }

  void SetUpOnMainThread() override {
    AliasResolver::SetObserverForTesting(
        base::BindRepeating(&CnameUncloakingBrowserTest::Resolved, base::Unretained(this)));
    cosmetic_filters::CosmeticFiltersTestBase::SetUpOnMainThread();
  }

  void TearDownOnMainThread() override {
    AliasResolver::SetObserverForTesting({});
    cosmetic_filters::CosmeticFiltersTestBase::TearDownOnMainThread();
  }

  // Loads a script from `host` on the HTTP server: "loaded" or "error". The
  // URL is built here: JsReplace drops a `$` that isn't a placeholder, so a
  // template literal's `${…}` can't be used in its script.
  std::string LoadScript(std::string_view host, std::string_view path) {
    return content::EvalJs(Tab(), content::JsReplace(R"(new Promise(done => {
               const script = document.createElement('script');
               script.src = $1;
               script.onload = () => done('loaded');
               script.onerror = () => done('error');
               document.head.append(script);
             }))",
                                                     Url(host, path)))
        .ExtractString();
  }

  void OpenPage() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page.html")));
  }

  int Blocked() { return protections::PageProtections::FromWebContents(Tab())->blocked_count(); }

  // The hosts the filter resolved, each at its start and with its result.
  std::vector<std::pair<std::string, int>> Resolutions() {
    base::AutoLock lock(lock_);
    return resolutions_;
  }

  bool WasResolved(std::string_view host) {
    for (const auto& [resolved, result] : Resolutions()) {
      if (resolved == host) {
        return true;
      }
    }
    return false;
  }

 private:
  void Resolved(const std::string& host, int result) {
    base::AutoLock lock(lock_);
    resolutions_.emplace_back(host, result);
  }

  base::Lock lock_;
  std::vector<std::pair<std::string, int>> resolutions_ GUARDED_BY(lock_);
};

// Standard checks no same-site URL, but checks its host's aliases.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, ASameSiteCloakedRequestNeverLeaves) {
  OpenPage();
  EXPECT_EQ(LoadScript("metrics.a.test", "/metrics.js"), "error");
  EXPECT_FALSE(LastRequestHeaders("/metrics.js")) << "the request reached the server";
  EXPECT_EQ(Blocked(), 1);
}

IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, ACrossSiteCloakedRequestIsBlocked) {
  OpenPage();
  EXPECT_EQ(LoadScript("stats.b.test", "/stats.js"), "error");
  EXPECT_FALSE(LastRequestHeaders("/stats.js"));
}

IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest,
                       AnExceptionForTheHostKeepsItsAliasesUnchecked) {
  OpenPage();
  EXPECT_EQ(LoadScript("allowed.a.test", "/allowed.js"), "loaded");
}

IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, AnAliasOnTheSameSiteIsNotChecked) {
  OpenPage();
  EXPECT_EQ(LoadScript("cdn.a.test", "/edge.js"), "loaded");
}

IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, OffResolvesNothing) {
  privacy_policy::SetLevel(GetProfile(), Url("a.test", "/"),
                           privacy_policy::ProtectionLevel::kOff);
  OpenPage();
  EXPECT_EQ(LoadScript("metrics.a.test", "/metrics.js"), "loaded");
  EXPECT_TRUE(Resolutions().empty());
}

IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, ARedirectToACloakedHostIsBlocked) {
  OpenPage();
  EXPECT_EQ(LoadScript("hop.a.test", "/hop.js"), "error");
  EXPECT_TRUE(LastRequestHeaders("/hop.js"));
  EXPECT_FALSE(LastRequestHeaders("/hop-target.js")) << "the redirect was followed";
}

// A blocked tracker's name server hears nothing from Shade.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, ABlockedUrlsHostIsNeverResolved) {
  OpenPage();
  EXPECT_EQ(LoadScript("blocked.test", "/blocked.js"), "error");
  EXPECT_EQ(LoadScript("metrics.a.test", "/metrics.js"), "error");
  EXPECT_FALSE(WasResolved("blocked.test"));
  EXPECT_TRUE(WasResolved("metrics.a.test")) << "the observer saw nothing at all";
}

IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, ARedirectRuleOnTheAliasAnswers) {
  OpenPage();
  EXPECT_EQ(LoadScript("widget.a.test", "/widget.js"), "loaded");
  EXPECT_FALSE(LastRequestHeaders("/widget.js")) << "the request reached the server";
  EXPECT_EQ(content::EvalJs(Tab(), "String(window.loaded)"), "undefined");
  EXPECT_EQ(Blocked(), 1);
}

IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, AFailedResolutionLetsTheRequestGo) {
  OpenPage();
  EXPECT_EQ(LoadScript("gone.a.test", "/gone.js"), "error");
  EXPECT_TRUE(WasResolved("gone.a.test"));
  EXPECT_EQ(Blocked(), 0);
}

// A chain of more than one alias reaches the filter (spike point 2).
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, EveryAliasIsChecked) {
  OpenPage();
  EXPECT_EQ(LoadScript("chain.a.test", "/chain.js"), "error");
  EXPECT_FALSE(LastRequestHeaders("/chain.js"));
}

// Through a proxy, the filter makes no local DNS query: the page itself is
// direct (bypassed), its subresource host isn't.
class CnameUncloakingProxyBrowserTest : public CnameUncloakingBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    CnameUncloakingBrowserTest::SetUpCommandLine(command_line);
    // Nothing listens on port 1: a proxied request fails to connect.
    command_line->AppendSwitchASCII("proxy-server", "127.0.0.1:1");
    command_line->AppendSwitchASCII("proxy-bypass-list", "a.test");
  }
};

IN_PROC_BROWSER_TEST_F(CnameUncloakingProxyBrowserTest, NothingIsResolvedBesideAProxy) {
  OpenPage();
  EXPECT_EQ(LoadScript("metrics.a.test", "/proxy.js"), "error");
  // The proxied host: started, then answered without a query. (The page's own
  // host is direct, and its other requests, the favicon's, resolve it.)
  std::vector<int> results;
  std::string seen;
  for (const auto& [host, result] : Resolutions()) {
    seen += host + "=" + net::ErrorToShortString(result) + " ";
    if (host == "metrics.a.test") {
      results.push_back(result);
    }
  }
  EXPECT_THAT(results, testing::ElementsAre(net::ERR_IO_PENDING, net::ERR_DNS_DIRECT_ONLY))
      << seen;
  EXPECT_EQ(Blocked(), 0);
}

}  // namespace
}  // namespace ghost::blocking
