// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_BLOCKING_REGISTRABLE_DOMAIN_H_
#define GHOST_COMPONENTS_BLOCKING_REGISTRABLE_DOMAIN_H_

#include <stddef.h>

#include <string_view>

#include "third_party/rust/cxx/v1/cxx.h"

namespace ghost::blocking {

// The site a host belongs to: its registrable domain (eTLD+1) under Chromium's
// registry, private registries included. An unknown top-level label (".test",
// an intranet name) counts as a registry, so "sub.a.test" and "a.test" are one
// site. A host with no domain (an IP address, "localhost") is its own site.
//
// The one definition of "the same site" for blocking: adblock-rust's
// third-party test calls it through the bridge (RegistrableDomainRange), and
// the request filter uses it to skip same-site requests.
std::string_view RegistrableDomain(std::string_view canonical_host);

// The domain's position in the host, as adblock-rust's ResolvesDomain wants
// it: host[start..end].
void RegistrableDomainRange(rust::Str canonical_host, size_t& start, size_t& end);

}  // namespace ghost::blocking

#endif  // GHOST_COMPONENTS_BLOCKING_REGISTRABLE_DOMAIN_H_
