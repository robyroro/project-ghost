// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_QUERY_FILTER_QUERY_FILTER_H_
#define GHOST_COMPONENTS_QUERY_FILTER_QUERY_FILTER_H_

#include <optional>
#include <string_view>

#include "url/gurl.h"
#include "url/origin.h"

namespace ghost::query_filter {

struct ParameterList;

// What is stripped: click identifiers only, or campaign parameters as well.
enum class Scope { kClick, kClickAndCampaign };

// |url| without the parameters |list| names for |scope|, or std::nullopt when
// it has none. The rest of the query is kept byte for byte, in order; the
// fragment is kept; an emptied query loses its '?'. Names are compared after
// percent-decoding, case-sensitively.
std::optional<GURL> Strip(const GURL& url, const ParameterList& list, Scope scope);

// An http(s) navigation with method GET: the only kind changed. A POST's body
// could be lost in an internal redirect.
bool IsFilteredNavigation(const GURL& url, std::string_view method);

// Whether a navigation to |url| brings it from elsewhere: started by the user
// (no initiator), by another site, or by an opaque origin.
bool ComesFromElsewhere(const GURL& url, const std::optional<url::Origin>& initiator);

// Whether a redirect from |from| to |to| crosses sites (ghost::RegistrableDomain:
// unknown registries count, an IP address is its own site).
bool CrossesSites(const GURL& from, const GURL& to);

}  // namespace ghost::query_filter

#endif  // GHOST_COMPONENTS_QUERY_FILTER_QUERY_FILTER_H_
