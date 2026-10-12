// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_THROTTLE_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_THROTTLE_H_

#include "base/memory/weak_ptr.h"
#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationThrottleRegistry;
}

namespace ghost::blocking {
struct CosmeticRules;
}

namespace ghost::cosmetic_filters {

// Prepares the hiding rules of the document a navigation commits: the page's
// level decides whether there are any, and the blocking engine, on its own
// sequence, which; the navigation waits for its answer before the response
// is processed, so the rules reach the renderer before the document is
// parsed (CosmeticFiltersTabHelper sends them).
class CosmeticFiltersThrottle : public content::NavigationThrottle {
 public:
  // Called for every navigation (patches/0042).
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  explicit CosmeticFiltersThrottle(content::NavigationThrottleRegistry& registry);
  CosmeticFiltersThrottle(const CosmeticFiltersThrottle&) = delete;
  CosmeticFiltersThrottle& operator=(const CosmeticFiltersThrottle&) = delete;
  ~CosmeticFiltersThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillProcessResponse() override;
  ThrottleCheckResult WillCommitWithoutUrlLoader() override;
  const char* GetNameForLogging() override;

 private:
  ThrottleCheckResult Prepare();
  void OnRules(blocking::CosmeticRules rules);

  base::WeakPtrFactory<CosmeticFiltersThrottle> weak_factory_{this};
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_THROTTLE_H_
