// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// WebSocket and WebTransport connections through the blocking engine
// (patches/0034), against test servers reached through host-resolver rules.

#include <set>
#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/web_transport_simple_test_server.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/embedded_test_server/install_default_websocket_handlers.h"

namespace ghost::blocking {
namespace {

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

constexpr char kSocketPath[] = "/echo-with-no-extension";

std::unique_ptr<HttpResponse> ServePage(const HttpRequest& request) {
  if (request.GetURL().path() == kSocketPath) {
    return nullptr;  // The WebSocket handlers answer.
  }
  auto response = std::make_unique<BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content("<html><head></head><body>page</body></html>");
  return response;
}

class ConnectionFilterBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    webtransport_server_.Start();
    webtransport_server_.SetUpCommandLine(command_line);
    // QUIC for the tracker's name too: unblocked, its WebTransport connects,
    // so a test that it is refused can fail.
    const int port = webtransport_server_.server_address().port();
    command_line->AppendSwitchASCII(
        switches::kOriginToForceQuicOn,
        base::StringPrintf("localhost:%d,tracker.test:%d,a.test:%d", port, port, port));
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestMonitor(
        base::BindRepeating(&ConnectionFilterBrowserTest::Record, base::Unretained(this)));
    net::test_server::InstallDefaultWebSocketHandlers(embedded_test_server());
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_test_server()->Start());
    embedded_https_test_server().SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    embedded_https_test_server().RegisterRequestHandler(base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_https_test_server().Start());
    ASSERT_TRUE(BlockingService::GetIfStarted());
    // A third-party tracker, and a rule for the socket's path that only a
    // check of same-site connections applies.
    BlockingService::GetIfStarted()->SetListsForTesting(
        {base::StrCat({"||tracker.test^$third-party\n", kSocketPath, "\n"})});
  }

  GURL Page(std::string_view host) { return embedded_test_server()->GetURL(host, "/page"); }
  GURL SecurePage(std::string_view host) {
    return embedded_https_test_server().GetURL(host, "/page");
  }
  GURL Socket(std::string_view host) {
    return net::test_server::GetWebSocketURL(*embedded_test_server(), std::string(host),
                                             kSocketPath);
  }
  GURL Transport(std::string_view host) {
    return GURL(base::StringPrintf("https://%s:%d/echo", std::string(host).c_str(),
                                   webtransport_server_.server_address().port()));
  }

  content::WebContents* Tab() { return browser()->tab_strip_model()->GetActiveWebContents(); }

  // "open" or "error".
  std::string OpenSocket(const GURL& url) {
    return content::EvalJs(Tab(), content::JsReplace(R"(new Promise(done => {
               const s = new WebSocket($1);
               s.onopen = () => { done('open'); s.close(); };
               s.onerror = () => done('error');
             }))",
                                                     url))
        .ExtractString();
  }

  std::string OpenSocketFromWorker(const GURL& url) {
    // The worker gets the URL in a message: no code is built from it.
    return content::EvalJs(Tab(), content::JsReplace(R"(new Promise(done => {
               const code = 'onmessage = e => {' +
                   ' const s = new WebSocket(e.data);' +
                   ' s.onopen = () => postMessage("open");' +
                   ' s.onerror = () => postMessage("error"); };';
               const w = new Worker(URL.createObjectURL(new Blob([code])));
               w.onmessage = e => done(e.data);
               w.postMessage($1);
             }))",
                                                     url.spec()))
        .ExtractString();
  }

  std::string OpenTransport(const GURL& url) {
    return content::EvalJs(Tab(), content::JsReplace(R"((async () => {
               try {
                 const t = new WebTransport($1);
                 await t.ready;
                 t.close();
                 return 'open';
               } catch (e) {
                 return 'error';
               }
             })())",
                                                     url))
        .ExtractString();
  }

  // Whether the WebSocket server saw a handshake for this host.
  bool Handshaken(std::string_view host) {
    base::AutoLock lock(lock_);
    return seen_.contains(std::string(host));
  }

  content::WebTransportSimpleTestServer webtransport_server_;

 private:
  void Record(const HttpRequest& request) {
    if (request.GetURL().path() != kSocketPath) {
      return;
    }
    base::AutoLock lock(lock_);
    std::string host = request.headers.at("Host");
    seen_.insert(host.substr(0, host.find(':')));
  }

  base::Lock lock_;
  std::set<std::string> seen_;
};

IN_PROC_BROWSER_TEST_F(ConnectionFilterBrowserTest, ATrackersWebSocketIsBlocked) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("a.test")));
  EXPECT_EQ(OpenSocket(Socket("tracker.test")), "error");
  EXPECT_FALSE(Handshaken("tracker.test")) << "the handshake reached the server";
}

IN_PROC_BROWSER_TEST_F(ConnectionFilterBrowserTest, ThePagesOwnWebSocketOpens) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("a.test")));
  EXPECT_EQ(OpenSocket(Socket("a.test")), "open");
  EXPECT_TRUE(Handshaken("a.test"));
}

IN_PROC_BROWSER_TEST_F(ConnectionFilterBrowserTest, StrictBlocksThePagesOwnWebSocket) {
  privacy_policy::SetLevel(GetProfile(), Page("a.test"), privacy_policy::ProtectionLevel::kStrict);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("a.test")));
  EXPECT_EQ(OpenSocket(Socket("a.test")), "error");
}

IN_PROC_BROWSER_TEST_F(ConnectionFilterBrowserTest, AnOffPagesTrackerWebSocketOpens) {
  privacy_policy::SetLevel(GetProfile(), Page("a.test"), privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("a.test")));
  EXPECT_EQ(OpenSocket(Socket("tracker.test")), "open");
}

IN_PROC_BROWSER_TEST_F(ConnectionFilterBrowserTest, AWorkersTrackerWebSocketIsBlocked) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("a.test")));
  EXPECT_EQ(OpenSocketFromWorker(Socket("tracker.test")), "error");
  EXPECT_FALSE(Handshaken("tracker.test"));
}

IN_PROC_BROWSER_TEST_F(ConnectionFilterBrowserTest, ATrackersWebTransportIsRefused) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), SecurePage("a.test")));
  EXPECT_EQ(OpenTransport(Transport("tracker.test")), "error");
}

IN_PROC_BROWSER_TEST_F(ConnectionFilterBrowserTest, AnOffPagesTrackerWebTransportConnects) {
  // The control for the test above: unblocked, the same connection opens.
  privacy_policy::SetLevel(GetProfile(), SecurePage("a.test"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), SecurePage("a.test")));
  EXPECT_EQ(OpenTransport(Transport("tracker.test")), "open");
}

}  // namespace
}  // namespace ghost::blocking
