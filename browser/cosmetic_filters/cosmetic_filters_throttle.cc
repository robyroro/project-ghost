// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/cosmetic_filters/cosmetic_filters_throttle.h"

#include <memory>

#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_tab_helper.h"
#include "ghost/browser/cosmetic_filters/prepared_rules.h"

namespace ghost::cosmetic_filters {

// static
void CosmeticFiltersThrottle::MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry) {
  CosmeticFiltersTabHelper::CreateForWebContents(registry.GetNavigationHandle().GetWebContents());
  registry.AddThrottle(std::make_unique<CosmeticFiltersThrottle>(registry));
}

CosmeticFiltersThrottle::CosmeticFiltersThrottle(content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

CosmeticFiltersThrottle::~CosmeticFiltersThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult CosmeticFiltersThrottle::WillProcessResponse() {
  return Prepare();
}

content::NavigationThrottle::ThrottleCheckResult
CosmeticFiltersThrottle::WillCommitWithoutUrlLoader() {
  return Prepare();
}

const char* CosmeticFiltersThrottle::GetNameForLogging() {
  return "GhostCosmeticFiltersThrottle";
}

content::NavigationThrottle::ThrottleCheckResult CosmeticFiltersThrottle::Prepare() {
  // Spike: fixed rules (Task 5 asks the engine).
  PreparedRules::CreateForNavigationHandle(*navigation_handle(),
                                           ".shade-spike{display:none !important}\n", true);
  return PROCEED;
}

}  // namespace ghost::cosmetic_filters
