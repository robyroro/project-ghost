// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_BLOCKING_DNS_ALIASES_H_
#define GHOST_COMPONENTS_BLOCKING_DNS_ALIASES_H_

#include <string>
#include <string_view>
#include <vector>

#include "url/gurl.h"

namespace ghost::blocking {

// CNAME uncloaking (Phase 4C): a request's host may be an alias of a
// tracker's. Of the host's DNS aliases (net's: canonical names, in no
// particular order), the ones worth checking with the lists: not the host
// itself, not an IP address or a name that isn't a canonical host, not one
// on the host's own site (a.site.com to b.site.com isn't cloaking), without
// a trailing dot, each once, sorted.
std::vector<std::string> AliasesToCheck(std::string_view host,
                                        const std::vector<std::string>& dns_aliases);

// `url` with `alias` as its host, the rest kept; an invalid GURL when
// `alias` isn't a canonical host.
GURL WithAlias(const GURL& url, std::string_view alias);

}  // namespace ghost::blocking

#endif  // GHOST_COMPONENTS_BLOCKING_DNS_ALIASES_H_
