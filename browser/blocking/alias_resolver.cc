// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/blocking/alias_resolver.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "net/base/address_list.h"
#include "net/base/net_errors.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "url/scheme_host_port.h"

namespace ghost::blocking {

namespace {

AliasResolver::Observer& TestObserver() {
  static base::NoDestructor<AliasResolver::Observer> observer;
  return *observer;
}

}  // namespace

AliasResolver::AliasResolver(network::mojom::NetworkContext* network_context,
                             const GURL& url,
                             const net::NetworkAnonymizationKey& key,
                             network::mojom::SecureDnsPolicy secure_dns_policy,
                             net::RequestPriority priority,
                             Callback callback)
    : host_(url.host()), callback_(std::move(callback)) {
  auto parameters = network::mojom::ResolveHostParameters::New();
  parameters->initial_priority = priority;
  parameters->secure_dns_policy = secure_dns_policy;
  // Never a local query for a host a proxy resolves.
  parameters->direct_only = true;
  parameters->control_handle = handle_.BindNewPipeAndPassReceiver();
  // The request's scheme, host and port, as its own resolution asks.
  network_context->ResolveHost(
      network::mojom::HostResolverHost::NewSchemeHostPort(url::SchemeHostPort(url)), key,
      std::move(parameters), receiver_.BindNewPipeAndPassRemote());
  // At the start: a query asked and then cancelled was still asked.
  if (!TestObserver().is_null()) {
    TestObserver().Run(host_, net::ERR_IO_PENDING);
  }
  // The network service went away: no answer.
  receiver_.set_disconnect_handler(base::BindOnce(&AliasResolver::Finish, base::Unretained(this),
                                                  net::ERR_FAILED, std::vector<std::string>()));
}

AliasResolver::~AliasResolver() {
  if (callback_ && handle_.is_bound()) {
    handle_->Cancel(net::ERR_ABORTED);
  }
}

// static
void AliasResolver::SetObserverForTesting(Observer observer) {
  TestObserver() = std::move(observer);
}

void AliasResolver::OnComplete(int result,
                               const net::ResolveErrorInfo& resolve_error_info,
                               const net::AddressList& resolved_addresses,
                               const net::HostResolverEndpointResults& alternative_endpoints) {
  Finish(result,
         result == net::OK ? resolved_addresses.dns_aliases() : std::vector<std::string>());
}

void AliasResolver::Finish(int result, std::vector<std::string> aliases) {
  if (!callback_) {
    return;
  }
  receiver_.reset();
  handle_.reset();
  if (!TestObserver().is_null()) {
    TestObserver().Run(host_, result);
  }
  // Last: the callback may destroy this.
  std::move(callback_).Run(std::move(aliases));
}

}  // namespace ghost::blocking
