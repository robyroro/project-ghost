// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/dns_aliases.h"

#include <set>

#include "base/strings/strcat.h"
#include "ghost/components/site/registrable_domain.h"

namespace ghost::blocking {

namespace {

std::string_view WithoutTrailingDot(std::string_view host) {
  return host.ends_with('.') ? host.substr(0, host.size() - 1) : host;
}

// A canonical host name: what a URL makes of it is itself, and it isn't an
// address.
bool IsCanonicalName(std::string_view alias) {
  if (alias.empty()) {
    return false;
  }
  const GURL probe(base::StrCat({"https://", alias, "/"}));
  return probe.is_valid() && probe.host() == alias && !probe.HostIsIPAddress();
}

}  // namespace

std::vector<std::string> AliasesToCheck(std::string_view host,
                                        const std::vector<std::string>& dns_aliases) {
  host = WithoutTrailingDot(host);
  const std::string_view site = RegistrableDomain(host);
  std::set<std::string> kept;
  for (const std::string& dns_alias : dns_aliases) {
    const std::string_view alias = WithoutTrailingDot(dns_alias);
    if (alias == host || !IsCanonicalName(alias) || RegistrableDomain(alias) == site) {
      continue;
    }
    kept.emplace(alias);
  }
  return std::vector<std::string>(kept.begin(), kept.end());
}

GURL WithAlias(const GURL& url, std::string_view alias) {
  if (!IsCanonicalName(alias)) {
    return GURL();
  }
  GURL::Replacements replacements;
  replacements.SetHostStr(alias);
  GURL result = url.ReplaceComponents(replacements);
  return result.is_valid() && result.host() == alias ? result : GURL();
}

}  // namespace ghost::blocking
