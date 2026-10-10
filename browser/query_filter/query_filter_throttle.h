// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_QUERY_FILTER_QUERY_FILTER_THROTTLE_H_
#define GHOST_BROWSER_QUERY_FILTER_QUERY_FILTER_THROTTLE_H_

#include <memory>

#include "ghost/components/query_filter/query_filter.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
}

namespace network {
struct ResourceRequest;
}

namespace ghost::query_filter {

// Strips tracking parameters from a top-level navigation whose URL comes from
// elsewhere: at the start (an internal redirect: the original URL is never
// sent) and at each redirect that crosses sites. The page commits at the
// clean URL.
class QueryFilterThrottle : public blink::URLLoaderThrottle {
 public:
  explicit QueryFilterThrottle(Scope scope);
  QueryFilterThrottle(const QueryFilterThrottle&) = delete;
  QueryFilterThrottle& operator=(const QueryFilterThrottle&) = delete;
  ~QueryFilterThrottle() override;

  // blink::URLLoaderThrottle:
  void WillStartRequest(network::ResourceRequest* request, bool* defer) override;
  void WillRedirectRequest(net::RedirectInfo* redirect_info,
                           const network::mojom::URLResponseHead& response_head,
                           bool* defer,
                           network::HttpRequestHeadersUpdateParams* headers_update_params) override;

 private:
  const Scope scope_;
  GURL url_;  // The URL being loaded, to judge the next redirect.
};

// A throttle for a navigation of the outermost main frame, in the scope the
// profile asks for; nullptr for anything else (iframes, subresources).
// ChromeContentBrowserClient::CreateURLLoaderThrottles calls it
// (patches/0032).
std::unique_ptr<blink::URLLoaderThrottle> MaybeCreateQueryFilterThrottle(
    const network::ResourceRequest& request,
    content::BrowserContext* browser_context);

}  // namespace ghost::query_filter

#endif  // GHOST_BROWSER_QUERY_FILTER_QUERY_FILTER_THROTTLE_H_
