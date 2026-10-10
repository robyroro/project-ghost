// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_BLOCKING_REQUEST_FILTER_H_
#define GHOST_BROWSER_BLOCKING_REQUEST_FILTER_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/self_deleting.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/self_deleting_url_loader_factory.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace network {
class URLLoaderFactoryBuilder;
struct ResourceRequest;
}  // namespace network

namespace net {
class IsolationInfo;
}

namespace ghost::blocking {

class BlockingEngine;

// Puts a RequestFilter in front of a URLLoaderFactory being created, once the
// blocking service has started. Called from
// ChromeContentBrowserClient::WillCreateURLLoaderFactory (patches/0029).
void MaybeProxyURLLoaderFactory(const net::IsolationInfo& isolation_info,
                                network::URLLoaderFactoryBuilder& factory_builder);

// Whether a request needs the engine's verdict (the Standard level,
// docs/privacy-model.md): top-level navigations always proceed, and requests
// to the page's own site aren't checked. `source` is the page the request is
// for; when it's unknown, every http(s) request is checked.
bool NeedsVerdict(const network::ResourceRequest& request, const GURL& url,
                  const GURL& source);

// A proxying URLLoaderFactory: asks the engine about each request that needs
// a verdict, holds it until the answer, then fails it with
// net::ERR_BLOCKED_BY_CLIENT or passes it on, and asks again on every
// redirect. Everything runs on the UI thread; the engine answers there.
class RequestFilter : public network::SelfDeletingURLLoaderFactory {
 public:
  using SharedTarget = base::RefCountedData<mojo::Remote<network::mojom::URLLoaderFactory>>;

  RequestFilter(mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
                mojo::PendingRemote<network::mojom::URLLoaderFactory> target,
                std::optional<url::Origin> top_frame_origin,
                BlockingEngine* engine,
                base::SelfDeletingPassKey pass_key);
  RequestFilter(const RequestFilter&) = delete;
  RequestFilter& operator=(const RequestFilter&) = delete;

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) override;

 private:
  class InFlight;

  ~RequestFilter() override;

  GURL SourceOf(const network::ResourceRequest& request) const;

  scoped_refptr<SharedTarget> target_;
  // The top-level page of the frame or worker this factory serves, when the
  // factory is created for one.
  const std::optional<url::Origin> top_frame_origin_;
  // The blocking service's engine, which is never destroyed.
  const raw_ptr<BlockingEngine> engine_;
};

}  // namespace ghost::blocking

#endif  // GHOST_BROWSER_BLOCKING_REQUEST_FILTER_H_
