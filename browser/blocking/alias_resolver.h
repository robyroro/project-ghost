// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_BLOCKING_ALIAS_RESOLVER_H_
#define GHOST_BROWSER_BLOCKING_ALIAS_RESOLVER_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/request_priority.h"
#include "services/network/public/cpp/resolve_host_client_base.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "url/gurl.h"

namespace network::mojom {
class NetworkContext;
}

namespace ghost::blocking {

// One host's DNS aliases, from the network service's host resolver (CNAME
// uncloaking, Phase 4C). It resolves with the request's key and secure DNS
// policy, so the request's own resolution finds the answer in the cache. It
// asks only for a direct connection: when a proxy would resolve the host, the
// answer is ERR_DNS_DIRECT_ONLY, with no aliases and no local query.
// Destroying it cancels the resolution.
class AliasResolver : public network::ResolveHostClientBase {
 public:
  // Runs once, never after destruction: `answered` when the host resolved
  // (with or without aliases); on any error, not answered and no aliases.
  using Callback = base::OnceCallback<void(bool answered, std::vector<std::string> aliases)>;
  // Called with each resolution's host when it starts (net::ERR_IO_PENDING),
  // and again with its result unless it was cancelled first.
  using Observer = base::RepeatingCallback<void(const std::string& host, int result)>;

  // Starts at once.
  AliasResolver(network::mojom::NetworkContext* network_context,
                const GURL& url,
                const net::NetworkAnonymizationKey& key,
                network::mojom::SecureDnsPolicy secure_dns_policy,
                net::RequestPriority priority,
                Callback callback);
  AliasResolver(const AliasResolver&) = delete;
  AliasResolver& operator=(const AliasResolver&) = delete;
  ~AliasResolver() override;

  static void SetObserverForTesting(Observer observer);

  // network::mojom::ResolveHostClient:
  void OnComplete(int result,
                  const net::ResolveErrorInfo& resolve_error_info,
                  const net::AddressList& resolved_addresses,
                  const net::HostResolverEndpointResults& alternative_endpoints) override;

 private:
  void Finish(int result, std::vector<std::string> aliases);

  const std::string host_;
  Callback callback_;
  mojo::Receiver<network::mojom::ResolveHostClient> receiver_{this};
  mojo::Remote<network::mojom::ResolveHostHandle> handle_;
};

}  // namespace ghost::blocking

#endif  // GHOST_BROWSER_BLOCKING_ALIAS_RESOLVER_H_
