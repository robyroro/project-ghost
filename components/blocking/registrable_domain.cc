// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/registrable_domain.h"

#include "ghost/components/site/registrable_domain.h"

namespace ghost::blocking {

void RegistrableDomainRange(rust::Str canonical_host, size_t& start, size_t& end) {
  const std::string_view host(canonical_host.data(), canonical_host.size());
  const std::string_view domain = RegistrableDomain(host);
  end = host.size();
  start = end - domain.size();
}

}  // namespace ghost::blocking
