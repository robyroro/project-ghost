// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The network defaults docs/privacy-model.md promises ("Cookies and storage",
// "Connections and DNS"), checked by their behavior where it can be observed:
// embedded test servers reached through host-resolver rules record every
// request, and a second server counts every connection made to it.

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/synchronization/lock.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/interstitials/security_interstitial_page_test_utils.h"
#include "chrome/browser/net/stub_resolver_config_reader.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/net/secure_dns_config.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_settings_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/https_upgrades_interceptor.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/privacy_sandbox/privacy_sandbox_prefs.h"
#include "components/privacy_sandbox/privacy_sandbox_settings.h"
#include "content/public/browser/btm_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/common/btm_utils.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/public/secure_dns_mode.h"
#include "net/dns/mock_host_resolver.h"
#include "net/socket/stream_socket.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/default_handlers.h"
#include "net/test/embedded_test_server/embedded_test_server_connection_listener.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "third_party/blink/public/mojom/peerconnection/webrtc_ip_handling_policy.mojom.h"

namespace ghost {
namespace {

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

// A 1x1 transparent GIF.
constexpr char kGif[] = "GIF89a\x01\x00\x01\x00\x80\x00\x00\x00\x00\x00\xff\xff\xff\x21\xf9"
                        "\x04\x01\x00\x00\x00\x00\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00"
                        "\x02\x02\x44\x01\x00\x3b";

// /worker.js reports the worker's navigator.globalPrivacyControl; /img is an
// image; anything else is a page.
std::unique_ptr<HttpResponse> Serve(const HttpRequest& request) {
  auto response = std::make_unique<BasicHttpResponse>();
  const std::string path(request.GetURL().path());
  if (path == "/worker.js") {
    response->set_content_type("text/javascript");
    response->set_content("postMessage(navigator.globalPrivacyControl);");
  } else if (path == "/img") {
    response->set_content_type("image/gif");
    response->set_content(std::string(kGif, sizeof(kGif) - 1));
  } else {
    response->set_content_type("text/html");
    response->set_content("<html><head></head><body>page</body></html>");
  }
  return response;
}

// Counts the connections a server accepts: a preconnect opens one without
// sending a request.
class ConnectionCounter : public net::test_server::EmbeddedTestServerConnectionListener {
 public:
  int accepted() const { return accepted_; }

  std::unique_ptr<net::StreamSocket> AcceptedSocket(
      std::unique_ptr<net::StreamSocket> socket) override {
    ++accepted_;
    return socket;
  }
  void ReadFromSocket(const net::StreamSocket& socket, int rv) override {}

 private:
  std::atomic<int> accepted_ = 0;
};

class NetworkDefaultsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestMonitor(
        base::BindRepeating(&NetworkDefaultsBrowserTest::Record, base::Unretained(this)));
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_test_server()->Start());

    embedded_https_test_server().SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    net::test_server::RegisterDefaultHandlers(&embedded_https_test_server());
    ASSERT_TRUE(embedded_https_test_server().Start());

    hints_server_.SetConnectionListener(&hints_connections_);
    hints_server_.RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(hints_server_.Start());
  }

  void TearDownOnMainThread() override {
    HttpsUpgradesInterceptor::SetHttpsPortForTesting(0);
    HttpsUpgradesInterceptor::SetHttpPortForTesting(0);
  }

  GURL HttpsUrl(std::string_view host, std::string_view path) {
    return embedded_https_test_server().GetURL(host, path);
  }

  std::string BodyText(content::RenderFrameHost* frame) {
    return content::EvalJs(frame, "document.body.innerText").ExtractString();
  }

  // Appends an iframe for |url| to the tab's page and returns it once loaded.
  content::RenderFrameHost* LoadIframe(const GURL& url) {
    content::TestNavigationObserver observer(Tab());
    EXPECT_TRUE(content::ExecJs(
        Tab(), content::JsReplace("const f = document.createElement('iframe');"
                                  " f.src = $1; document.body.appendChild(f);",
                                  url)));
    observer.Wait();
    return content::ChildFrameAt(Tab()->GetPrimaryMainFrame(), 0);
  }

  // Lets time pass, for a test that something does not happen.
  void WaitFor(base::TimeDelta delay) {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), delay);
    run_loop.Run();
  }

  // A site the user hasn't chosen to visit; every socket it accepts counts.
  net::EmbeddedTestServer hints_server_;
  ConnectionCounter hints_connections_;

  GURL Url(std::string_view host, std::string_view path) {
    return embedded_test_server()->GetURL(host, path);
  }

  content::WebContents* Tab(Browser* browser = nullptr) {
    return (browser ? browser : this->browser())->tab_strip_model()->GetActiveWebContents();
  }

  // A header of the last request the server received for this host and
  // path; empty if absent.
  std::string Header(std::string_view host, std::string_view path, const std::string& name) {
    base::AutoLock lock(lock_);
    auto request = requests_.find(base::StrCat({host, path}));
    if (request == requests_.end()) {
      return "<no request>";
    }
    auto header = request->second.find(name);
    return header == request->second.end() ? "" : header->second;
  }

  void LoadImage(const GURL& src) {
    ASSERT_TRUE(content::ExecJs(
        Tab(), content::JsReplace("new Promise(done => { const i = new Image();"
                                  " i.onload = i.onerror = done; i.src = $1; })",
                                  src)));
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

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, GpcIsSentOnNavigationsAndSubresources) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(Header("a.test", "/page", "Sec-GPC"), "1");
  LoadImage(Url("b.test", "/img"));
  EXPECT_EQ(Header("b.test", "/img", "Sec-GPC"), "1");
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, GpcIsVisibleToPagesAndWorkers) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  EXPECT_EQ(content::EvalJs(Tab(), "navigator.globalPrivacyControl"), true);
  EXPECT_EQ(content::EvalJs(Tab(),
                            "new Promise(done => { const w = new Worker('/worker.js');"
                            " w.onmessage = e => done(e.data); })"),
            true);
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, WebRtcUsesOnlyTheDefaultPublicInterface) {
  for (Browser* b : {browser(), CreateIncognitoBrowser()}) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(b, Url("a.test", "/page")));
    // What the renderer's WebRTC reads.
    EXPECT_EQ(Tab(b)->GetMutableRendererPrefs()->webrtc_ip_handling_policy,
              blink::mojom::WebRtcIpHandlingPolicy::kDefaultPublicInterfaceOnly);
  }
}

// Upstream turns them off on a profile's first run when third-party cookies
// are blocked (PrivacySandboxServiceImpl::MaybeInitializeRelatedWebsiteSetsPref),
// which Shade's default does: they follow from it, so no default of their own.
IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, RelatedWebsiteSetsAreOff) {
  EXPECT_FALSE(PrivacySandboxSettingsFactory::GetForProfile(GetProfile())
                   ->AreRelatedWebsiteSetsEnabled());
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, PrivacySandboxAdApisAreOff) {
  privacy_sandbox::PrivacySandboxSettings* settings =
      PrivacySandboxSettingsFactory::GetForProfile(GetProfile());
  EXPECT_FALSE(settings->IsTopicsAllowed());
  EXPECT_FALSE(settings->IsFledgeAllowed(url::Origin::Create(GURL("https://a.test")),
                                         url::Origin::Create(GURL("https://b.test")),
                                         privacy_sandbox::InterestGroupApiOperation::kJoin));
  // Ad measurement has no settings method at 152: this pref is its decision.
  EXPECT_FALSE(
      GetProfile()->GetPrefs()->GetBoolean(prefs::kPrivacySandboxM1AdMeasurementEnabled));
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, ThirdPartyCookiesAreNotSent) {
  // Set by b.test as the site visited, then asked for in b.test's frame on
  // another site.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), HttpsUrl("b.test", "/set-cookie?tp=1;SameSite=None;Secure")));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), HttpsUrl("b.test", "/echoheader?Cookie")));
  ASSERT_EQ(BodyText(Tab()->GetPrimaryMainFrame()), "tp=1");  // The control.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), HttpsUrl("a.test", "/echoheader?x")));
  EXPECT_EQ(BodyText(LoadIframe(HttpsUrl("b.test", "/echoheader?Cookie"))), "None");
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, BounceTrackingMitigationIsOn) {
  EXPECT_TRUE(base::FeatureList::IsEnabled(features::kBtm));
  EXPECT_NE(features::kBtmTriggeringAction.Get(), content::BtmTriggeringAction::kNone);
  EXPECT_TRUE(content::BtmService::Get(GetProfile()));
}

// Strict HTTPS-First, as Private mode promises, differs from balanced on hosts
// that aren't unique (an intranet name, .test): strict warns before loading
// one over HTTP, balanced lets it load. Upstream's Incognito is balanced-like.
IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, IncognitoIsStrictAndNormalBalanced) {
  HttpsUpgradesInterceptor::SetHttpPortForTesting(embedded_test_server()->port());
  HttpsUpgradesInterceptor::SetHttpsPortForTesting(embedded_test_server()->port());
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, Url("intranet.test", "/page")));
  EXPECT_TRUE(chrome_browser_interstitials::IsShowingHttpsFirstModeInterstitial(Tab(incognito)));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("intranet.test", "/page")));
  EXPECT_FALSE(chrome_browser_interstitials::IsShowingHttpsFirstModeInterstitial(Tab()));
  // The pref stays in Incognito's memory: Normal's own is untouched.
  EXPECT_FALSE(GetProfile()->GetPrefs()->GetBoolean(prefs::kHttpsOnlyModeEnabled));
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, DnsOverHttpsIsAutomatic) {
  EXPECT_EQ(SystemNetworkContextManager::GetStubResolverConfigReader()
                ->GetSecureDnsConfiguration(
                    /*force_check_parental_controls_for_automatic_mode=*/false)
                .mode(),
            net::SecureDnsMode::kAutomatic);
}

IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, ACrossSiteSubresourceGetsOnlyTheOrigin) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page?secret=1")));
  LoadImage(Url("b.test", "/img"));
  EXPECT_EQ(Header("b.test", "/img", "Referer"), Url("a.test", "/").spec());
}

// <link rel=preconnect> and rel=dns-prefetch reach PreconnectManagerImpl,
// which checks the preloading pref before connecting and before resolving;
// only the connection can be observed here.
IN_PROC_BROWSER_TEST_F(NetworkDefaultsBrowserTest, PageHintsAndSpeculationRulesContactNobody) {
  const GURL target = hints_server_.GetURL("hints.test", "/target");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page")));
  ASSERT_TRUE(content::ExecJs(Tab(), content::JsReplace(R"(
      for (const rel of ['preconnect', 'dns-prefetch']) {
        const l = document.createElement('link');
        l.rel = rel;
        l.href = $1;
        document.head.appendChild(l);
      }
      const s = document.createElement('script');
      s.type = 'speculationrules';
      s.textContent = JSON.stringify({prefetch: [{source: 'list', urls: [$1]}]});
      document.head.appendChild(s);)",
                                                        target)));
  WaitFor(base::Seconds(3));
  EXPECT_EQ(hints_connections_.accepted(), 0) << "a socket was opened to a site not visited";
}

}  // namespace
}  // namespace ghost
