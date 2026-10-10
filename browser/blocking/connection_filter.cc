// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/blocking/connection_filter.h"

#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/blocking/request_filter.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/protections/page_protections.h"
#include "ghost/components/blocking/blocking_engine.h"
#include "net/cookies/site_for_cookies.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ghost::blocking {
namespace {

privacy_policy::EffectivePolicy PolicyOf(content::BrowserContext* context, const GURL& page) {
  return context ? privacy_policy::GetPolicy(context, page)
                 : privacy_policy::PolicyFor(privacy_policy::ProtectionLevel::kStandard,
                                             /*campaign_pref=*/false);
}

GURL PageOf(content::RenderFrameHost* frame) {
  return frame->GetOutermostMainFrame()->GetLastCommittedURL();
}

// The engine reads a WebSocket's type from its ws: or wss: scheme; a
// WebTransport URL (https:) is "other", as an empty destination maps. A
// blocked connection counts for |frame|'s tab (the protections panel).
void Judge(const GURL& url,
           const GURL& page,
           const privacy_policy::EffectivePolicy& policy,
           content::GlobalRenderFrameHostId frame,
           base::OnceCallback<void(bool blocked)> done) {
  BlockingService* service = BlockingService::GetIfStarted();
  if (!service || !policy.block_requests || !ChecksAgainst(url, page, policy.check_same_site)) {
    std::move(done).Run(false);
    return;
  }
  service->engine().Check(
      {url, page, network::mojom::RequestDestination::kEmpty, "GET"},
      base::BindOnce(
          [](content::GlobalRenderFrameHostId frame, base::OnceCallback<void(bool)> done,
             Decision decision) {
            if (decision.blocked) {
              protections::PageProtections::RecordBlocked(frame);
            }
            std::move(done).Run(decision.blocked);
          },
          frame, std::move(done)));
}

}  // namespace

bool ShouldInterceptWebSocket() {
  return BlockingService::GetIfStarted();
}

void FilterWebSocket(content::RenderFrameHost* frame,
                     const GURL& url,
                     const net::SiteForCookies& site_for_cookies,
                     base::OnceCallback<void(bool blocked)> done) {
  const GURL page = frame ? PageOf(frame) : site_for_cookies.RepresentativeUrl();
  Judge(url, page, PolicyOf(frame ? frame->GetBrowserContext() : nullptr, page),
        frame ? frame->GetGlobalId() : content::GlobalRenderFrameHostId(), std::move(done));
}

void FilterWebTransport(int process_id,
                        int frame_routing_id,
                        const GURL& url,
                        const url::Origin& initiator_origin,
                        base::OnceCallback<void(bool blocked)> done) {
  content::RenderFrameHost* frame = content::RenderFrameHost::FromID(process_id, frame_routing_id);
  content::RenderProcessHost* process = content::RenderProcessHost::FromID(process_id);
  const GURL page = frame ? PageOf(frame) : initiator_origin.GetURL();
  Judge(url, page, PolicyOf(process ? process->GetBrowserContext() : nullptr, page),
        content::GlobalRenderFrameHostId(process_id, frame_routing_id), std::move(done));
}

}  // namespace ghost::blocking
