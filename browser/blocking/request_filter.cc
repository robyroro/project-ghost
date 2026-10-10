// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/blocking/request_filter.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/memory/self_deleting.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/components/blocking/blocking_engine.h"
#include "ghost/components/blocking/registrable_domain.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace ghost::blocking {

void MaybeProxyURLLoaderFactory(const net::IsolationInfo& isolation_info,
                                network::URLLoaderFactoryBuilder& factory_builder) {
  BlockingService* service = BlockingService::GetIfStarted();
  if (!service) {
    return;
  }
  auto [receiver, target] = factory_builder.Append();
  base::MakeSelfDeleting<RequestFilter>(std::move(receiver), std::move(target),
                                        isolation_info.top_frame_origin(), &service->engine());
}

bool NeedsVerdict(const network::ResourceRequest& request, const GURL& url,
                  const GURL& source) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  // The top-level document: what the user navigated to always loads. Frames
  // (iframes, fenced frames) have their own destinations and are checked.
  if (request.destination == network::mojom::RequestDestination::kDocument) {
    return false;
  }
  if (source.is_valid() && source.has_host() &&
      RegistrableDomain(url.host()) == RegistrableDomain(source.host())) {
    return false;
  }
  return true;
}

class RequestFilter::InFlight : public network::mojom::URLLoader,
                                public network::mojom::URLLoaderClient {
 public:
  InFlight(scoped_refptr<SharedTarget> target,
           BlockingEngine* engine,
           mojo::PendingReceiver<network::mojom::URLLoader> loader,
           int32_t request_id,
           uint32_t options,
           const network::ResourceRequest& request,
           mojo::PendingRemote<network::mojom::URLLoaderClient> client,
           const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
           GURL source)
      : target_(std::move(target)),
        engine_(engine),
        pending_loader_(std::move(loader)),
        pending_client_(std::move(client)),
        request_id_(request_id),
        options_(options),
        request_(request),
        traffic_annotation_(traffic_annotation),
        source_(std::move(source)) {}

  InFlight(const InFlight&) = delete;
  InFlight& operator=(const InFlight&) = delete;
  ~InFlight() override = default;

  void Start() {
    if (!NeedsVerdict(request_, request_.url, source_)) {
      // A same-site request starts at once, but stays here: it may redirect
      // to a tracker.
      OnStartVerdict(Decision());
      return;
    }
    engine_->Check(Check(request_.url),
                   base::BindOnce(&InFlight::OnStartVerdict, weak_factory_.GetWeakPtr()));
  }

  // network::mojom::URLLoader:
  void FollowRedirect(network::HttpRequestHeadersUpdateParams headers_update_params,
                      const std::optional<GURL>& new_url) override {
    target_loader_->FollowRedirect(std::move(headers_update_params), new_url);
  }
  void SetPriority(net::RequestPriority priority, int32_t intra_priority_value) override {
    target_loader_->SetPriority(priority, intra_priority_value);
  }

  // network::mojom::URLLoaderClient:
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr early_hints) override {
    client_->OnReceiveEarlyHints(std::move(early_hints));
  }
  void OnReceiveResponse(network::mojom::URLResponseHeadPtr head,
                         mojo::ScopedDataPipeConsumerHandle body,
                         std::optional<mojo_base::BigBuffer> cached_metadata) override {
    client_->OnReceiveResponse(std::move(head), std::move(body), std::move(cached_metadata));
  }
  void OnReceiveRedirect(const net::RedirectInfo& redirect_info,
                         network::mojom::URLResponseHeadPtr head) override {
    if (!NeedsVerdict(request_, redirect_info.new_url, source_)) {
      client_->OnReceiveRedirect(redirect_info, std::move(head));
      return;
    }
    // A redirect to a tracker is checked like a request to it.
    engine_->Check(Check(redirect_info.new_url),
                   base::BindOnce(&InFlight::OnRedirectVerdict, weak_factory_.GetWeakPtr(),
                                  redirect_info, std::move(head)));
  }
  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        OnUploadProgressCallback callback) override {
    client_->OnUploadProgress(current_position, total_size, std::move(callback));
  }
  void OnTransferSizeUpdated(int32_t transfer_size_diff) override {
    client_->OnTransferSizeUpdated(transfer_size_diff);
  }
  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    completed_ = true;
    client_->OnComplete(status);
  }

 private:
  CheckRequest Check(const GURL& url) const {
    return {url, source_, request_.destination, request_.method};
  }

  void OnStartVerdict(Decision decision) {
    client_.Bind(std::move(pending_client_));
    if (decision.blocked) {
      Block(decision);
      return;
    }
    loader_receiver_.Bind(std::move(pending_loader_));
    loader_receiver_.set_disconnect_handler(
        base::BindOnce(&InFlight::OnPageGone, base::Unretained(this)));
    client_.set_disconnect_handler(base::BindOnce(&InFlight::OnPageGone, base::Unretained(this)));
    target_->data->CreateLoaderAndStart(target_loader_.BindNewPipeAndPassReceiver(), request_id_,
                                        options_, request_,
                                        client_receiver_.BindNewPipeAndPassRemote(),
                                        traffic_annotation_);
    client_receiver_.set_disconnect_handler(
        base::BindOnce(&InFlight::OnNetworkGone, base::Unretained(this)));
  }

  void OnRedirectVerdict(net::RedirectInfo redirect_info,
                         network::mojom::URLResponseHeadPtr head,
                         Decision decision) {
    if (decision.blocked) {
      // Closing the network side cancels the request there.
      target_loader_.reset();
      client_receiver_.reset();
      Block(decision);
      return;
    }
    client_->OnReceiveRedirect(redirect_info, std::move(head));
  }

  void Block(const Decision& decision) {
    VLOG(1) << "Blocked " << request_.url << " by " << decision.filter;
    client_->OnComplete(network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
    delete this;
  }

  // The page dropped the request: closing the network side cancels it.
  void OnPageGone() { delete this; }

  void OnNetworkGone() {
    if (!completed_) {
      client_->OnComplete(network::URLLoaderCompletionStatus(net::ERR_FAILED));
    }
    delete this;
  }

  const scoped_refptr<SharedTarget> target_;
  const raw_ptr<BlockingEngine> engine_;
  mojo::PendingReceiver<network::mojom::URLLoader> pending_loader_;
  mojo::PendingRemote<network::mojom::URLLoaderClient> pending_client_;
  const int32_t request_id_;
  const uint32_t options_;
  const network::ResourceRequest request_;
  const net::MutableNetworkTrafficAnnotationTag traffic_annotation_;
  const GURL source_;

  // The page's end.
  mojo::Receiver<network::mojom::URLLoader> loader_receiver_{this};
  mojo::Remote<network::mojom::URLLoaderClient> client_;
  // The network's end.
  mojo::Remote<network::mojom::URLLoader> target_loader_;
  mojo::Receiver<network::mojom::URLLoaderClient> client_receiver_{this};
  bool completed_ = false;

  base::WeakPtrFactory<InFlight> weak_factory_{this};
};

RequestFilter::RequestFilter(mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
                             mojo::PendingRemote<network::mojom::URLLoaderFactory> target,
                             std::optional<url::Origin> top_frame_origin,
                             BlockingEngine* engine,
                             base::SelfDeletingPassKey pass_key)
    : network::SelfDeletingURLLoaderFactory(std::move(receiver), pass_key),
      target_(base::MakeRefCounted<SharedTarget>()),
      top_frame_origin_(std::move(top_frame_origin)),
      engine_(engine) {
  target_->data.Bind(std::move(target));
  target_->data.set_disconnect_handler(base::BindOnce(
      &RequestFilter::DisconnectReceiversAndDestroy, base::Unretained(this)));
}

RequestFilter::~RequestFilter() = default;

GURL RequestFilter::SourceOf(const network::ResourceRequest& request) const {
  if (top_frame_origin_ && !top_frame_origin_->opaque()) {
    return top_frame_origin_->GetURL();
  }
  // Browser-initiated loads (frame navigations) carry the page in their
  // trusted parameters.
  if (request.trusted_params) {
    const std::optional<url::Origin>& top =
        request.trusted_params->isolation_info.top_frame_origin();
    if (top && !top->opaque()) {
      return top->GetURL();
    }
  }
  if (request.request_initiator && !request.request_initiator->opaque()) {
    return request.request_initiator->GetURL();
  }
  return GURL();
}

void RequestFilter::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  // Only what can never need a verdict skips the filter: anything else, even
  // a same-site request, may redirect to a tracker.
  if (!request.url.SchemeIsHTTPOrHTTPS() ||
      request.destination == network::mojom::RequestDestination::kDocument) {
    target_->data->CreateLoaderAndStart(std::move(loader), request_id, options, request,
                                        std::move(client), traffic_annotation);
    return;
  }
  GURL source = SourceOf(request);
  // Owns itself until the request ends.
  (new InFlight(target_, engine_, std::move(loader), request_id, options, request,
                std::move(client), traffic_annotation, std::move(source)))
      ->Start();
}

}  // namespace ghost::blocking
