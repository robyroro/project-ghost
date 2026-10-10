// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_SITE_REGISTRABLE_DOMAIN_H_
#define GHOST_COMPONENTS_SITE_REGISTRABLE_DOMAIN_H_

#include <string_view>

namespace ghost {

// The site a host belongs to: its registrable domain (eTLD+1) under Chromium's
// registry, private registries included. An unknown top-level label (".test",
// an intranet name) counts as a registry, so "sub.a.test" and "a.test" are one
// site, which net's SameDomainOrHost() doesn't do. A host with no domain (an
// IP address, "localhost") is its own site.
//
// The one definition of "the same site" in Shade: blocking (adblock-rust's
// third-party test through the bridge, the request filter's same-site
// exemption) and the query filter use it.
std::string_view RegistrableDomain(std::string_view canonical_host);

}  // namespace ghost

#endif  // GHOST_COMPONENTS_SITE_REGISTRABLE_DOMAIN_H_
