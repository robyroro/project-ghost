// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_BLOCKING_CONNECTION_FILTER_H_
#define GHOST_BROWSER_BLOCKING_CONNECTION_FILTER_H_

#include "base/functional/callback.h"

class GURL;

namespace content {
class RenderFrameHost;
}

namespace net {
class SiteForCookies;
}

namespace url {
class Origin;
}

namespace ghost::blocking {

// WebSocket and WebTransport connections don't go through a URLLoaderFactory,
// so the request filter never sees them. ChromeContentBrowserClient's
// connection hooks (patches/0034) ask these instead: the blocking engine
// judges the connection at its page's protection level, as the request
// filter judges requests.

// Whether every WebSocket should go through CreateWebSocket, where
// FilterWebSocket judges it: once the blocking service has started.
bool ShouldInterceptWebSocket();

// Runs |done| with whether the connection is blocked. The page is the frame's
// outermost main frame; without a frame (a shared or service worker), the site
// of |site_for_cookies|, at the Standard level (the hook gives no profile).
void FilterWebSocket(content::RenderFrameHost* frame,
                     const GURL& url,
                     const net::SiteForCookies& site_for_cookies,
                     base::OnceCallback<void(bool blocked)> done);

// The same for WebTransport; without a frame, the page is the initiator's
// origin, and the process gives the profile.
void FilterWebTransport(int process_id,
                        int frame_routing_id,
                        const GURL& url,
                        const url::Origin& initiator_origin,
                        base::OnceCallback<void(bool blocked)> done);

}  // namespace ghost::blocking

#endif  // GHOST_BROWSER_BLOCKING_CONNECTION_FILTER_H_
