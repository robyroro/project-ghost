// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/query_filter/query_filter_throttle.h"

#include <optional>

#include "content/public/browser/browser_context.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/components/query_filter/parameter_list.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"

namespace ghost::query_filter {

QueryFilterThrottle::QueryFilterThrottle(base::WeakPtr<content::BrowserContext> context,
                                         bool drop_referrer)
    : context_(std::move(context)), drop_referrer_(drop_referrer) {}

QueryFilterThrottle::~QueryFilterThrottle() = default;

std::optional<Scope> QueryFilterThrottle::ScopeFor(const GURL& url) const {
  if (!context_) {
    return std::nullopt;
  }
  const privacy_policy::EffectivePolicy policy = privacy_policy::GetPolicy(context_.get(), url);
  if (!policy.strip_click_identifiers) {
    return std::nullopt;
  }
  return policy.strip_campaign_parameters ? Scope::kClickAndCampaign : Scope::kClick;
}

void QueryFilterThrottle::WillStartRequest(network::ResourceRequest* request, bool* defer) {
  url_ = request->url;
  if (drop_referrer_) {
    // NO_REFERRER: the redirects carry none either.
    request->referrer = GURL();
    request->referrer_policy = net::ReferrerPolicy::NO_REFERRER;
  }
  if (!IsFilteredNavigation(request->url, request->method) ||
      !ComesFromElsewhere(request->url, request->request_initiator)) {
    return;
  }
  const std::optional<Scope> scope = ScopeFor(request->url);
  if (!scope) {
    return;
  }
  if (std::optional<GURL> clean = Strip(request->url, ShippedParameterList(), *scope)) {
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
  const std::optional<Scope> scope = ScopeFor(redirect_info->new_url);
  if (!scope) {
    return;
  }
  if (std::optional<GURL> clean =
          Strip(redirect_info->new_url, ShippedParameterList(), *scope)) {
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
  // A Strict page's cross-site navigation leaves without Referer: the level
  // of the page the navigation comes from.
  bool drop_referrer = false;
  if (request.request_initiator && !request.request_initiator->opaque()) {
    const GURL initiator = request.request_initiator->GetURL();
    drop_referrer =
        !privacy_policy::GetPolicy(browser_context, initiator).cross_site_referrer &&
        CrossesSites(initiator, request.url);
  }
  return std::make_unique<QueryFilterThrottle>(browser_context->GetWeakPtr(), drop_referrer);
}

}  // namespace ghost::query_filter
