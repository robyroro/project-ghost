// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/features/feature_overrides.h"

#include <functional>

#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/common/chrome_features.h"

namespace ghost {

void AppendFeatureOverrides(
    std::vector<base::FeatureList::FeatureOverrideInfo>* overrides) {
  // HTTPS-First in balanced mode for users who have not chosen a setting.
  // Upstream consults the kHttpsFirstBalancedMode pref only after the user has
  // set it; until then this feature decides, so a pref default would do
  // nothing.
  overrides->emplace_back(
      std::cref(features::kHttpsFirstBalancedModeAutoEnable),
      base::FeatureList::OVERRIDE_ENABLE_FEATURE);

  // The Media Router (Cast) probes the local network with mDNS and DIAL. Its
  // kEnableMediaRouter pref is honoured only when set by enterprise policy, so
  // the feature is the only switch available to us.
  overrides->emplace_back(std::cref(media_router::kMediaRouter),
                          base::FeatureList::OVERRIDE_DISABLE_FEATURE);
}

}  // namespace ghost
