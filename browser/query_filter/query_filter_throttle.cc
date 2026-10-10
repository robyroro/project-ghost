// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/query_filter/query_filter_throttle.h"

#include <optional>

#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "ghost/browser/query_filter/prefs.h"
#include "ghost/components/query_filter/parameter_list.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"

namespace ghost::query_filter {

QueryFilterThrottle::QueryFilterThrottle(Scope scope) : scope_(scope) {}

QueryFilterThrottle::~QueryFilterThrottle() = default;

void QueryFilterThrottle::WillStartRequest(network::ResourceRequest* request, bool* defer) {
  url_ = request->url;
  if (!IsFilteredNavigation(request->url, request->method) ||
      !ComesFromElsewhere(request->url, request->request_initiator)) {
    return;
  }
  if (std::optional<GURL> clean = Strip(request->url, ShippedParameterList(), scope_)) {
    request->url = *clean;
    url_ = *clean;
  }
}

void QueryFilterThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& response_head,
    bool* defer,
    network::HttpRequestHeadersUpdateParams* headers_update_params) {
  const GURL from = url_;
  url_ = redirect_info->new_url;
  if (!IsFilteredNavigation(redirect_info->new_url, redirect_info->new_method) ||
      !CrossesSites(from, redirect_info->new_url)) {
    return;
  }
  if (std::optional<GURL> clean = Strip(redirect_info->new_url, ShippedParameterList(), scope_)) {
    redirect_info->new_url = *clean;
    url_ = *clean;
  }
}

std::unique_ptr<blink::URLLoaderThrottle> MaybeCreateQueryFilterThrottle(
    const network::ResourceRequest& request,
    content::BrowserContext* browser_context) {
  if (!request.is_outermost_main_frame ||
      request.destination != network::mojom::RequestDestination::kDocument) {
    return nullptr;
  }
  const bool campaign =
      browser_context->IsOffTheRecord() ||
      user_prefs::UserPrefs::Get(browser_context)->GetBoolean(kStripCampaignParametersPref);
  return std::make_unique<QueryFilterThrottle>(campaign ? Scope::kClickAndCampaign
                                                        : Scope::kClick);
}

}  // namespace ghost::query_filter
