// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_BLOCKING_ALIAS_MEMO_H_
#define GHOST_COMPONENTS_BLOCKING_ALIAS_MEMO_H_

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/base/network_anonymization_key.h"

namespace ghost::blocking {

// The DNS aliases a page's requests were given, for a minute (CNAME
// uncloaking, Phase 4C). A page asks for the same hosts again and again; each
// answer from the network service costs a round trip between processes, which
// waits behind the page's loading on both sides. The memo answers the later
// requests at once. It holds what the host resolver answered, keyed as the
// resolution was asked: the network anonymization key, the host and whether
// secure DNS was off. Errors aren't kept (a proxy's ERR_DNS_DIRECT_ONLY
// included): the next request asks again. One per URLLoaderFactory, so a memo
// lives as long as the page or worker it serves.
class AliasMemo : public base::RefCounted<AliasMemo> {
 public:
  // The host cache keeps every successful resolution at least this long
  // (kMinimumTTLSeconds in net/dns/host_resolver_manager_job.cc), so the memo
  // never answers with aliases the host cache would no longer have.
  static constexpr base::TimeDelta kLifetime = base::Minutes(1);
  // Past this many hosts, the expired entries go; if none had, all do.
  static constexpr size_t kMaxEntries = 256;

  AliasMemo();
  AliasMemo(const AliasMemo&) = delete;
  AliasMemo& operator=(const AliasMemo&) = delete;

  // The aliases remembered for the key, if an answer younger than kLifetime
  // is; null otherwise.
  const std::vector<std::string>* Find(const net::NetworkAnonymizationKey& key,
                                       const std::string& host,
                                       bool secure_dns_disabled,
                                       base::TimeTicks now) const;
  void Remember(const net::NetworkAnonymizationKey& key,
                const std::string& host,
                bool secure_dns_disabled,
                std::vector<std::string> aliases,
                base::TimeTicks now);

  size_t size() const { return entries_.size(); }

 private:
  friend class base::RefCounted<AliasMemo>;
  ~AliasMemo();

  using Key = std::tuple<net::NetworkAnonymizationKey, std::string, bool>;
  struct Entry {
    std::vector<std::string> aliases;
    base::TimeTicks expires;
  };

  std::map<Key, Entry> entries_;
};

}  // namespace ghost::blocking

#endif  // GHOST_COMPONENTS_BLOCKING_ALIAS_MEMO_H_
