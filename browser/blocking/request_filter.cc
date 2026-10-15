// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/blocking/request_filter.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/memory/self_deleting.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "ghost/browser/blocking/alias_resolver.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/protections/page_protections.h"
#include "ghost/components/blocking/blocking_engine.h"
#include "ghost/components/blocking/dns_aliases.h"
#include "ghost/components/site/registrable_domain.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/data_url.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_version.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/early_hints.mojom.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace ghost::blocking {

namespace {

// A factory can outlive its profile's last use: a gone profile gets the
// Standard policy.
privacy_policy::EffectivePolicy PolicyIfAlive(base::WeakPtr<content::BrowserContext> context,
                                              const GURL& page) {
  return context ? privacy_policy::GetPolicy(context.get(), page)
                 : privacy_policy::PolicyFor(privacy_policy::ProtectionLevel::kStandard,
                                             /*campaign_pref=*/false);
}

// Whether a request's Referer would name another site than the one it goes to.
bool HasCrossSiteReferrer(const network::ResourceRequest& request) {
  return request.referrer.is_valid() && request.referrer.has_host() &&
         RegistrableDomain(request.referrer.host()) != RegistrableDomain(request.url.host());
}

// The response type a page would see for this request had the network
// answered (Fetch: basic for its own origin, opaque for a no-cors request
// elsewhere, cors otherwise).
network::mojom::FetchResponseType ResponseTypeFor(const network::ResourceRequest& request) {
  if (request.request_initiator && request.request_initiator->IsSameOriginWith(request.url)) {
    return network::mojom::FetchResponseType::kBasic;
  }
  return request.mode == network::mojom::RequestMode::kNoCors
             ? network::mojom::FetchResponseType::kOpaque
             : network::mojom::FetchResponseType::kCors;
}

// The network context of the frame's storage partition; without a frame (a
// shared or service worker), the profile's default one. Null once the
// profile is gone.
network::mojom::NetworkContext* NetworkContextOf(base::WeakPtr<content::BrowserContext> context,
                                                 content::GlobalRenderFrameHostId frame) {
  if (content::RenderFrameHost* host = content::RenderFrameHost::FromID(frame)) {
    return host->GetStoragePartition()->GetNetworkContext();
  }
  return context ? context->GetDefaultStoragePartition()->GetNetworkContext() : nullptr;
}

}  // namespace

void MaybeProxyURLLoaderFactory(content::BrowserContext* context,
                                content::RenderFrameHost* frame,
                                const net::IsolationInfo& isolation_info,
                                network::URLLoaderFactoryBuilder& factory_builder) {
  BlockingService* service = BlockingService::GetIfStarted();
  if (!service) {
    return;
  }
  const content::GlobalRenderFrameHostId frame_id =
      frame ? frame->GetGlobalId() : content::GlobalRenderFrameHostId();
  UncloakContext uncloak;
  uncloak.network_context =
      base::BindRepeating(&NetworkContextOf, context->GetWeakPtr(), frame_id);
  uncloak.network_anonymization_key = isolation_info.network_anonymization_key();
  auto [receiver, target] = factory_builder.Append();
  base::MakeSelfDeleting<RequestFilter>(
      std::move(receiver), std::move(target), isolation_info.top_frame_origin(), frame_id,
      &service->engine(), base::BindRepeating(&PolicyIfAlive, context->GetWeakPtr()),
      std::move(uncloak));
}

bool NeedsVerdict(const network::ResourceRequest& request, const GURL& url,
                  const GURL& source, bool check_same_site) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  // The top-level document: what the user navigated to always loads. Frames
  // (iframes, fenced frames) have their own destinations and are checked.
  if (request.destination == network::mojom::RequestDestination::kDocument) {
    return false;
  }
  return ChecksAgainst(url, source, check_same_site);
}

bool ChecksAgainst(const GURL& url, const GURL& page, bool check_same_site) {
  if (check_same_site || !page.is_valid() || !page.has_host()) {
    return true;
  }
  return RegistrableDomain(url.host()) != RegistrableDomain(page.host());
}

class RequestFilter::InFlight : public network::mojom::URLLoader,
                                public network::mojom::URLLoaderClient {
 public:
  InFlight(scoped_refptr<SharedTarget> target,
           content::GlobalRenderFrameHostId frame,
           BlockingEngine* engine,
           mojo::PendingReceiver<network::mojom::URLLoader> loader,
           int32_t request_id,
           uint32_t options,
           const network::ResourceRequest& request,
           mojo::PendingRemote<network::mojom::URLLoaderClient> client,
           const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
           GURL source,
           bool check_same_site,
           UncloakContext uncloak)
      : target_(std::move(target)),
        frame_(frame),
        engine_(engine),
        pending_loader_(std::move(loader)),
        pending_client_(std::move(client)),
        request_id_(request_id),
        options_(options),
        request_(request),
        traffic_annotation_(traffic_annotation),
        source_(std::move(source)),
        check_same_site_(check_same_site),
        uncloak_(std::move(uncloak)) {}

  InFlight(const InFlight&) = delete;
  InFlight& operator=(const InFlight&) = delete;
  ~InFlight() override = default;

  void Start() {
    // The page's end at once: a page that drops the request while its host
    // resolves ends it, and the resolution with it. The loader waits for the
    // verdict: a SetPriority or FollowRedirect before then would reach an
    // unbound target_loader_.
    client_.Bind(std::move(pending_client_));
    client_.set_disconnect_handler(base::BindOnce(&InFlight::OnPageGone, base::Unretained(this)));
    auto done = base::BindOnce(&InFlight::OnStartVerdict, weak_factory_.GetWeakPtr());
    if (!NeedsVerdict(request_, request_.url, source_, check_same_site_)) {
      // A same-site request isn't checked by its URL (Standard), but its
      // host's aliases are: an alias on another site is a third party. It
      // stays here after: it may redirect to a tracker.
      Uncloak(request_.url, std::move(done));
      return;
    }
    engine_->Check(Check(request_.url),
                   base::BindOnce(&InFlight::OnUrlVerdict, weak_factory_.GetWeakPtr(),
                                  request_.url, std::move(done)));
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
    // A redirect to a tracker is checked like a request to it, aliases
    // included.
    auto done = base::BindOnce(&InFlight::OnRedirectVerdict, weak_factory_.GetWeakPtr(),
                               redirect_info, std::move(head));
    if (!NeedsVerdict(request_, redirect_info.new_url, source_, check_same_site_)) {
      Uncloak(redirect_info.new_url, std::move(done));
      return;
    }
    engine_->Check(Check(redirect_info.new_url),
                   base::BindOnce(&InFlight::OnUrlVerdict, weak_factory_.GetWeakPtr(),
                                  redirect_info.new_url, std::move(done)));
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

  // The URL's verdict: blocked ends it, before any DNS query, so a blocked
  // tracker's name server hears nothing; otherwise the host's aliases decide.
  void OnUrlVerdict(GURL url, base::OnceCallback<void(Decision)> done, Decision decision) {
    if (decision.blocked) {
      std::move(done).Run(std::move(decision));
      return;
    }
    Uncloak(url, std::move(done));
  }

  // Resolves the URL's host, then has the engine judge its aliases (CNAME
  // uncloaking). No aliases, or no answer: not blocked. A host the factory's
  // requests resolved less than a minute ago is judged at once, with the
  // aliases they were given.
  void Uncloak(const GURL& url, base::OnceCallback<void(Decision)> done) {
    network::mojom::NetworkContext* network_context =
        uncloak_.network_context ? uncloak_.network_context.Run() : nullptr;
    if (!network_context || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
        url.HostIsIPAddress() || net::IsLocalhost(url)) {
      std::move(done).Run(Decision());
      return;
    }
    const net::NetworkAnonymizationKey& key =
        request_.trusted_params
            ? request_.trusted_params->isolation_info.network_anonymization_key()
            : uncloak_.network_anonymization_key;
    const bool secure_dns_disabled =
        request_.trusted_params && request_.trusted_params->disable_secure_dns;
    const base::TimeTicks start = base::TimeTicks::Now();
    if (const std::vector<std::string>* aliases =
            uncloak_.memo->Find(key, std::string(url.host()), secure_dns_disabled, start)) {
      Judge(url, start, /*resolution=*/std::nullopt, *aliases, std::move(done));
      return;
    }
    resolver_ = std::make_unique<AliasResolver>(
        network_context, url, key,
        secure_dns_disabled ? network::mojom::SecureDnsPolicy::DISABLE
                            : network::mojom::SecureDnsPolicy::ALLOW,
        request_.priority,
        base::BindOnce(&InFlight::OnAliases, weak_factory_.GetWeakPtr(), url, key,
                       secure_dns_disabled, start, std::move(done)));
  }

  void OnAliases(GURL url,
                 net::NetworkAnonymizationKey key,
                 bool secure_dns_disabled,
                 base::TimeTicks start,
                 base::OnceCallback<void(Decision)> done,
                 bool answered,
                 std::vector<std::string> aliases) {
    // The resolver ran this as its last act: it can go.
    resolver_.reset();
    const base::TimeTicks now = base::TimeTicks::Now();
    if (answered) {
      uncloak_.memo->Remember(key, std::string(url.host()), secure_dns_disabled, aliases, now);
    }
    Judge(url, start, now - start, aliases, std::move(done));
  }

  // Has the engine judge the aliases worth checking. `resolution` is the
  // resolution's time, or nullopt for aliases the memo gave.
  void Judge(const GURL& url,
             base::TimeTicks start,
             std::optional<base::TimeDelta> resolution,
             const std::vector<std::string>& aliases,
             base::OnceCallback<void(Decision)> done) {
    std::vector<GURL> alias_urls;
    for (const std::string& alias : AliasesToCheck(url.host(), aliases)) {
      if (GURL alias_url = WithAlias(url, alias); alias_url.is_valid()) {
        alias_urls.push_back(std::move(alias_url));
      }
    }
    // The total, and the resolution's part of it (the rest is the aliases'
    // check), read by tools/uncloak_cost.py (the budget).
    auto timed = base::BindOnce(
        [](std::string host, base::TimeTicks start, std::optional<base::TimeDelta> resolution,
           base::OnceCallback<void(Decision)> done, Decision decision) {
          VLOG(2) << "Uncloak " << host << ": "
                  << (base::TimeTicks::Now() - start).InMicroseconds() << " us ("
                  << (resolution ? "resolution " +
                                       base::NumberToString(resolution->InMicroseconds()) + " us"
                                 : std::string("remembered"))
                  << ")";
          std::move(done).Run(std::move(decision));
        },
        std::string(url.host()), start, resolution, std::move(done));
    if (alias_urls.empty()) {
      std::move(timed).Run(Decision());
      return;
    }
    engine_->CheckAliases(Check(url), std::move(alias_urls), std::move(timed));
  }

  void OnStartVerdict(Decision decision) {
    if (decision.blocked) {
      Block(decision);
      return;
    }
    loader_receiver_.Bind(std::move(pending_loader_));
    loader_receiver_.set_disconnect_handler(
        base::BindOnce(&InFlight::OnPageGone, base::Unretained(this)));
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
    VLOG(1) << "Blocked " << request_.url
            << (decision.alias.empty() ? "" : " (alias " + decision.alias + ")") << " by "
            << decision.filter;
    protections::PageProtections::RecordBlocked(frame_);
    if (decision.redirect.empty() || !Answer(decision.redirect)) {
      client_->OnComplete(network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
    }
    delete this;
  }

  // Answers with one of Shade's resources (a redirect rule), without the
  // network. False if the data: URL doesn't parse or the pipe can't be made.
  bool Answer(const std::string& data_url) {
    std::string mime_type;
    std::string charset;
    std::string body;
    if (!net::DataURL::Parse(GURL(data_url), &mime_type, &charset, &body)) {
      return false;
    }
    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;
    if (mojo::CreateDataPipe(std::max<size_t>(body.size(), 1), producer, consumer) !=
        MOJO_RESULT_OK) {
      return false;
    }
    if (!body.empty() && producer->WriteAllData(base::as_byte_span(body)) != MOJO_RESULT_OK) {
      return false;
    }
    auto head = network::mojom::URLResponseHead::New();
    head->mime_type = mime_type;
    head->charset = charset;
    head->headers = net::HttpResponseHeaders::Builder(net::HttpVersion(1, 1), "200 OK")
                        .AddHeader("Content-Type", mime_type)
                        .AddHeader("Access-Control-Allow-Origin", "*")
                        .Build();
    head->response_type = ResponseTypeFor(request_);
    head->content_length = body.size();
    client_->OnReceiveResponse(std::move(head), std::move(consumer), std::nullopt);
    network::URLLoaderCompletionStatus status(net::OK);
    status.decoded_body_length = base::ByteSize(body.size());
    client_->OnComplete(status);
    return true;
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
  const content::GlobalRenderFrameHostId frame_;
  const raw_ptr<BlockingEngine> engine_;
  mojo::PendingReceiver<network::mojom::URLLoader> pending_loader_;
  mojo::PendingRemote<network::mojom::URLLoaderClient> pending_client_;
  const int32_t request_id_;
  const uint32_t options_;
  const network::ResourceRequest request_;
  const net::MutableNetworkTrafficAnnotationTag traffic_annotation_;
  const GURL source_;
  const bool check_same_site_;
  const UncloakContext uncloak_;
  // The resolution in progress, if any.
  std::unique_ptr<AliasResolver> resolver_;

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
                             content::GlobalRenderFrameHostId frame,
                             BlockingEngine* engine,
                             PolicyCallback policy,
                             UncloakContext uncloak,
                             base::SelfDeletingPassKey pass_key)
    : network::SelfDeletingURLLoaderFactory(std::move(receiver), pass_key),
      target_(base::MakeRefCounted<SharedTarget>()),
      top_frame_origin_(std::move(top_frame_origin)),
      frame_(frame),
      engine_(engine),
      policy_(std::move(policy)),
      uncloak_(std::move(uncloak)) {
  target_->data.Bind(std::move(target));
  target_->data.set_disconnect_handler(base::BindOnce(
      &RequestFilter::DisconnectReceiversAndDestroy, base::Unretained(this)));
}

RequestFilter::~RequestFilter() = default;

UncloakContext::UncloakContext() = default;
UncloakContext::UncloakContext(const UncloakContext&) = default;
UncloakContext& UncloakContext::operator=(const UncloakContext&) = default;
UncloakContext::~UncloakContext() = default;

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
  const privacy_policy::EffectivePolicy policy = policy_.Run(source);
  // Strict: a cross-site request leaves without Referer, and its redirects too.
  std::optional<network::ResourceRequest> without_referrer;
  if (!policy.cross_site_referrer && HasCrossSiteReferrer(request)) {
    without_referrer.emplace(request);
    without_referrer->referrer = GURL();
    without_referrer->referrer_policy = net::ReferrerPolicy::NO_REFERRER;
  }
  const network::ResourceRequest& to_send = without_referrer ? *without_referrer : request;
  if (!policy.block_requests) {
    target_->data->CreateLoaderAndStart(std::move(loader), request_id, options, to_send,
                                        std::move(client), traffic_annotation);
    return;
  }
  // Owns itself until the request ends.
  (new InFlight(target_, frame_, engine_, std::move(loader), request_id, options, to_send,
                std::move(client), traffic_annotation, std::move(source),
                policy.check_same_site, uncloak_))
      ->Start();
}

}  // namespace ghost::blocking
