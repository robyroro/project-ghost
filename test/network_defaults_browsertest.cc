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
#include "chrome/browser/privacy_sandbox/privacy_sandbox_settings_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/privacy_sandbox/privacy_sandbox_settings.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/socket/stream_socket.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
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
  }

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

}  // namespace
}  // namespace ghost
