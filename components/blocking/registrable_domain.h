// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_BLOCKING_REGISTRABLE_DOMAIN_H_
#define GHOST_COMPONENTS_BLOCKING_REGISTRABLE_DOMAIN_H_

#include <stddef.h>

#include <string_view>

#include "third_party/rust/cxx/v1/cxx.h"

namespace ghost::blocking {

// The domain's position in the host, as adblock-rust's ResolvesDomain wants
// it: host[start..end]. The domain is ghost::RegistrableDomain()'s
// (//ghost/components/site), so blocking and the browser agree on what a site is.
void RegistrableDomainRange(rust::Str canonical_host, size_t& start, size_t& end);

}  // namespace ghost::blocking

#endif  // GHOST_COMPONENTS_BLOCKING_REGISTRABLE_DOMAIN_H_
