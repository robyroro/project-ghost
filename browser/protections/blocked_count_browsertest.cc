// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Each request or connection the blocking engine blocks counts for the tab
// whose page made it (patches/0037), against test servers reached through
// host-resolver rules.

#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/protections/page_protections.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/embedded_test_server/install_default_websocket_handlers.h"
#include "ui/base/window_open_disposition.h"

namespace ghost::protections {
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

class BlockedCountBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    net::test_server::InstallDefaultWebSocketHandlers(embedded_test_server());
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(blocking::BlockingService::GetIfStarted());
    blocking::BlockingService::GetIfStarted()->SetListsForTesting(
        {"||tracker.test^$third-party\n"});
  }

  GURL Page(std::string_view host) { return embedded_test_server()->GetURL(host, "/page"); }

  content::WebContents* Tab() { return browser()->tab_strip_model()->GetActiveWebContents(); }

  static int Count(content::WebContents* contents) {
    PageProtections* protections = PageProtections::FromWebContents(contents);
    return protections ? protections->blocked_count() : 0;
  }

  // Loads |n| images from |host| in |target| and waits until each has
  // loaded or failed.
  void LoadImages(const content::ToRenderFrameHost& target, std::string_view host, int n) {
    EXPECT_EQ(content::EvalJs(target, content::JsReplace(R"(Promise.all(
                  Array.from({length: $2}, (_, i) => new Promise(done => {
                    const img = new Image();
                    img.onload = img.onerror = done;
                    img.src = $1 + '?' + i;
                  }))).then(() => 'done'))",
                                                         Page(host), n)),
              "done");
  }
};

IN_PROC_BROWSER_TEST_F(BlockedCountBrowserTest, CountsThePagesBlockedRequests) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  LoadImages(Tab(), "tracker.test", 3);
  LoadImages(Tab(), "news.test", 2);  // allowed: not counted
  EXPECT_EQ(Count(Tab()), 3);
}

IN_PROC_BROWSER_TEST_F(BlockedCountBrowserTest, CountsSubframesForTheirTab) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  ASSERT_TRUE(content::ExecJs(Tab(), content::JsReplace(R"(new Promise(done => {
                const frame = document.createElement('iframe');
                frame.onload = done;
                frame.src = $1;
                document.body.appendChild(frame);
              }))",
                                                         Page("frame.test"))));
  content::RenderFrameHost* frame = content::ChildFrameAt(Tab(), 0);
  ASSERT_TRUE(frame);
  LoadImages(frame, "tracker.test", 1);
  EXPECT_EQ(Count(Tab()), 1);
}

IN_PROC_BROWSER_TEST_F(BlockedCountBrowserTest, CountsDedicatedWorkers) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  // The worker gets the URL in a message: no code is built from it.
  EXPECT_EQ(content::EvalJs(Tab(), content::JsReplace(R"(new Promise(done => {
                const code = 'onmessage = e => fetch(e.data).then(' +
                    '() => postMessage("loaded"), () => postMessage("failed"));';
                const w = new Worker(URL.createObjectURL(new Blob([code])));
                w.onmessage = e => done(e.data);
                w.postMessage($1);
              }))",
                                                       Page("tracker.test").spec())),
            "failed");
  EXPECT_EQ(Count(Tab()), 1);
}

IN_PROC_BROWSER_TEST_F(BlockedCountBrowserTest, CountsBlockedWebSockets) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  GURL socket = net::test_server::GetWebSocketURL(*embedded_test_server(), "tracker.test",
                                                  kSocketPath);
  EXPECT_EQ(content::EvalJs(Tab(), content::JsReplace(R"(new Promise(done => {
                const s = new WebSocket($1);
                s.onopen = () => { done('open'); s.close(); };
                s.onerror = () => done('error');
              }))",
                                                       socket)),
            "error");
  EXPECT_EQ(Count(Tab()), 1);
}

IN_PROC_BROWSER_TEST_F(BlockedCountBrowserTest, ANavigationStartsFromZero) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  LoadImages(Tab(), "tracker.test", 2);
  ASSERT_EQ(Count(Tab()), 2);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("other.test")));
  EXPECT_EQ(Count(Tab()), 0);
}

IN_PROC_BROWSER_TEST_F(BlockedCountBrowserTest, ABackgroundTabCountsForItself) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  content::WebContents* active = Tab();
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), Page("shop.test"), WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  content::WebContents* background = browser()->tab_strip_model()->GetWebContentsAt(1);
  ASSERT_NE(background, active);
  ASSERT_EQ(Tab(), active);
  LoadImages(background, "tracker.test", 3);
  EXPECT_EQ(Count(background), 3);
  EXPECT_EQ(Count(active), 0);
}

}  // namespace
}  // namespace ghost::protections
