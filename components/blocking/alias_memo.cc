// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/alias_memo.h"

#include <utility>

#include "base/containers/map_util.h"

namespace ghost::blocking {

AliasMemo::AliasMemo() = default;
AliasMemo::~AliasMemo() = default;

const std::vector<std::string>* AliasMemo::Find(const net::NetworkAnonymizationKey& key,
                                                const std::string& host,
                                                bool secure_dns_disabled,
                                                base::TimeTicks now) const {
  const Entry* entry = base::FindOrNull(entries_, Key(key, host, secure_dns_disabled));
  return entry && now < entry->expires ? &entry->aliases : nullptr;
}

void AliasMemo::Remember(const net::NetworkAnonymizationKey& key,
                         const std::string& host,
                         bool secure_dns_disabled,
                         std::vector<std::string> aliases,
                         base::TimeTicks now) {
  if (entries_.size() >= kMaxEntries) {
    std::erase_if(entries_, [now](const auto& item) { return item.second.expires <= now; });
    if (entries_.size() >= kMaxEntries) {
      entries_.clear();
    }
  }
  entries_.insert_or_assign(Key(key, host, secure_dns_disabled),
                            Entry{std::move(aliases), now + kLifetime});
}

}  // namespace ghost::blocking
