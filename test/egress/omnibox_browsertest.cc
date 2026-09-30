// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the browser sends while the user searches from the omnibox. The egress
// audit (tools/egress_audit.py) drives pages over DevTools, which cannot reach
// the browser's own UI, so the omnibox is covered here: every request any part
// of the browser makes is recorded and answered locally, and a test fails on
// anything the user did not ask for.

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/url_loader_interceptor.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost {
namespace {

// Records every http(s) request made while it exists, from pages and from the
// browser process alike, and answers each with an empty page.
class RequestRecorder {
 public:
  RequestRecorder()
      : interceptor_(base::BindRepeating(&RequestRecorder::Intercept,
                                         base::Unretained(this))) {}

  std::vector<GURL> requests() {
    base::AutoLock lock(lock_);
    return requests_;
  }

 private:
  // Runs on the UI or the IO thread, depending on the factory.
  bool Intercept(content::URLLoaderInterceptor::RequestParams* params) {
    const GURL& url = params->url_request.url;
    if (!url.SchemeIsHTTPOrHTTPS()) {
      return false;
    }
    {
      base::AutoLock lock(lock_);
      requests_.push_back(url);
    }
    content::URLLoaderInterceptor::WriteResponse(
        "HTTP/1.1 200 OK\nContent-Type: text/html\n\n", "<p>results",
        params->client.get());
    return true;
  }

  base::Lock lock_;
  std::vector<GURL> requests_ GUARDED_BY(lock_);
  // Last, so it stops calling Intercept() before the members it uses go.
  content::URLLoaderInterceptor interceptor_;
};

class OmniboxEgressBrowserTest : public InProcessBrowserTest {
 protected:
  LocationBar* location_bar() {
    return BrowserWindow::FromBrowser(browser())->GetLocationBar();
  }
};

// Suggestions and zero-suggest would send the text to the search engine as it
// is typed, before the user decides to search (browser/prefs).
IN_PROC_BROWSER_TEST_F(OmniboxEgressBrowserTest, TypingSendsNothing) {
  RequestRecorder recorder;
  location_bar()->GetOmniboxController()->edit_model()->OnSetFocus(
      /*control_down=*/false);
  location_bar()->GetOmniboxView()->SetUserText(u"ghost browser privacy");
  ui_test_utils::WaitForAutocompleteDone(browser());

  EXPECT_THAT(recorder.requests(), testing::IsEmpty());
}

// A single word could also be an intranet host name. Upstream can probe it
// with a request to http://<word>/, which puts the search term on the local
// network; its default leaves the probe off, and a policy turns it on.
IN_PROC_BROWSER_TEST_F(OmniboxEgressBrowserTest,
                       SearchContactsOnlyTheSearchEngine) {
  RequestRecorder recorder;
  content::TestNavigationObserver navigation(
      chrome_test_utils::GetActiveWebContents(this));
  ui_test_utils::SendToOmniboxAndSubmit(browser(), "ghost");
  navigation.Wait();

  std::vector<GURL> requests = recorder.requests();
  ASSERT_FALSE(requests.empty());
  EXPECT_EQ(requests.front().spec().rfind("https://duckduckgo.com/?q=ghost", 0),
            0u)
      << requests.front();
  for (const GURL& url : requests) {
    EXPECT_EQ(url.host(), "duckduckgo.com") << url;
  }
}

}  // namespace
}  // namespace ghost
