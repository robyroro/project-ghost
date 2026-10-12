// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TEST_BASE_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TEST_BASE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/strings/string_split.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "net/test/embedded_test_server/http_request.h"
#include "url/gurl.h"

class Browser;

namespace content {
class ToRenderFrameHost;
class WebContents;
}  // namespace content

namespace net::test_server {
class HttpResponse;
}

namespace ghost::cosmetic_filters {

// Cosmetic filtering through the browser, the renderer and the engine: pages
// on every host name (host-resolver rules send them all to the test
// servers), a test list, and helpers to read what the page shows.
class CosmeticFiltersTestBase : public InProcessBrowserTest {
 protected:
  CosmeticFiltersTestBase();
  ~CosmeticFiltersTestBase() override;

  // The test's list.
  virtual std::string List() const = 0;
  // Called before the servers start: add the pages here.
  virtual void AddPages() = 0;

  // `html` inside <!doctype html><html><body>...</body></html> at `path`.
  void AddPage(std::string path, std::string html, base::StringPairs headers = {});
  // Any other resource, as is.
  void AddResource(std::string path,
                   std::string content_type,
                   std::string body,
                   base::StringPairs headers = {});

  void SetUpOnMainThread() override;

  GURL Url(std::string_view host, std::string_view path);
  GURL HttpsUrl(std::string_view host, std::string_view path);
  content::WebContents* Tab(Browser* browser = nullptr);
  // The element's computed display, at once.
  std::string Display(const content::ToRenderFrameHost& frame, std::string_view id);
  // The display of what `script` finds, once it is `expected`, or after 2 s:
  // generic and procedural rules arrive after the page's first paint. The
  // script is pasted in, not eval'd: a page's CSP may forbid eval.
  std::string WaitForDisplayOf(const content::ToRenderFrameHost& frame,
                               std::string_view script,
                               std::string_view expected);
  std::string WaitForDisplay(const content::ToRenderFrameHost& frame,
                             std::string_view id,
                             std::string_view expected);
  // Waits as long as a scanner would take to answer (20 times its batch
  // interval), for checks that something stays shown.
  void WaitForAScanner(content::WebContents* tab);
  // The headers of the last request the servers got for `path`, if any.
  std::optional<net::test_server::HttpRequest::HeaderMap> LastRequestHeaders(
      std::string_view path);

 private:
  struct Resource {
    std::string content_type;
    std::string body;
    base::StringPairs headers;
  };

  std::unique_ptr<net::test_server::HttpResponse> Serve(
      const net::test_server::HttpRequest& request);
  void Record(const net::test_server::HttpRequest& request);

  // Filled before the servers start, read-only after.
  std::map<std::string, Resource> resources_;
  base::Lock lock_;
  std::map<std::string, net::test_server::HttpRequest::HeaderMap> requests_ GUARDED_BY(lock_);
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TEST_BASE_H_
