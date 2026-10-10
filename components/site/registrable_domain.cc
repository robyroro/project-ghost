// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/site/registrable_domain.h"

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/url_util.h"

namespace ghost {

namespace rcd = net::registry_controlled_domains;

std::string_view RegistrableDomain(std::string_view host) {
  // An IP address has no domain; counting unknown registries would otherwise
  // take its last number for a top-level label.
  if (url::HostIsIPAddress(host)) {
    return host;
  }
  const size_t registry = rcd::GetCanonicalHostRegistryLength(
      host, rcd::INCLUDE_UNKNOWN_REGISTRIES, rcd::INCLUDE_PRIVATE_REGISTRIES);
  // 0: no registry (an IP address, a single label); npos: not a valid host.
  // A host that is all registry ("com", "github.io") has no domain either.
  if (registry == 0 || registry == std::string_view::npos ||
      registry + 1 >= host.size()) {
    return host;
  }
  const size_t registry_dot = host.size() - registry - 1;
  const size_t label_dot = host.rfind('.', registry_dot - 1);
  return host.substr(label_dot == std::string_view::npos ? 0 : label_dot + 1);
}

}  // namespace ghost
