// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/query_filter/query_filter.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "ghost/components/query_filter/parameter_list.h"
#include "ghost/components/site/registrable_domain.h"

namespace ghost::query_filter {
namespace {

bool Matches(const std::vector<Parameter>& parameters, std::string_view name,
             std::string_view site) {
  return std::ranges::any_of(parameters, [&](const Parameter& parameter) {
    const bool name_matches =
        parameter.prefix ? name.starts_with(parameter.name) : name == parameter.name;
    return name_matches &&
           (parameter.sites.empty() || std::ranges::contains(parameter.sites, site));
  });
}

}  // namespace

std::optional<GURL> Strip(const GURL& url, const ParameterList& list, Scope scope) {
  if (!url.is_valid() || !url.has_query()) {
    return std::nullopt;
  }
  const std::string_view site = RegistrableDomain(url.host());
  std::vector<std::string_view> kept;
  bool stripped = false;
  for (std::string_view token :
       base::SplitStringPiece(url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    const std::string name = base::UnescapeBinaryURLComponent(token.substr(0, token.find('=')));
    if (Matches(list.click, name, site) ||
        (scope == Scope::kClickAndCampaign && Matches(list.campaign, name, site))) {
      stripped = true;
    } else {
      kept.push_back(token);
    }
  }
  if (!stripped) {
    return std::nullopt;
  }
  const std::string query = base::JoinString(kept, "&");
  GURL::Replacements replacements;
  if (query.empty()) {
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(query);
  }
  return url.ReplaceComponents(replacements);
}

bool IsFilteredNavigation(const GURL& url, std::string_view method) {
  return url.SchemeIsHTTPOrHTTPS() && method == "GET";
}

bool ComesFromElsewhere(const GURL& url, const std::optional<url::Origin>& initiator) {
  if (!initiator || initiator->opaque()) {
    return true;
  }
  return RegistrableDomain(url.host()) != RegistrableDomain(initiator->host());
}

bool CrossesSites(const GURL& from, const GURL& to) {
  return RegistrableDomain(from.host()) != RegistrableDomain(to.host());
}

}  // namespace ghost::query_filter
