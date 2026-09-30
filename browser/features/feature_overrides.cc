// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/features/feature_overrides.h"

#include <functional>

#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/common/chrome_features.h"
#include "components/autofill/core/common/autofill_debug_features.h"
#include "components/network_time/network_time_tracker.h"
#include "components/omnibox/browser/aim_eligibility_service_features.h"

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

  // The network time tracker asks clients2.google.com for the time in the
  // background, so even an idle browser contacts Google periodically. Without
  // it, certificate errors caused by a wrong clock are recognised by comparing
  // against the build time instead (ssl_errors::GetClockState); upstream makes
  // the same trade on ChromeOS, where this feature is off by default.
  overrides->emplace_back(
      std::cref(network_time::kNetworkTimeServiceQuerying),
      base::FeatureList::OVERRIDE_DISABLE_FEATURE);

  // AI Mode is a Google Search product. Its eligibility service asks
  // www.google.com at startup whatever the default search engine is.
  // kAimEnabled is upstream's kill switch for the whole feature.
  overrides->emplace_back(std::cref(omnibox::kAimEnabled),
                          base::FeatureList::OVERRIDE_DISABLE_FEATURE);

  // Autofill asks content-autofill.googleapis.com to classify the fields of
  // every form the user sees, sending the form's structure, and uploads votes
  // after forms are submitted. Without it, Autofill still fills forms from
  // its local heuristics. Upstream uses this feature to keep tests hermetic.
  overrides->emplace_back(
      std::cref(autofill::features::debug::kAutofillServerCommunication),
      base::FeatureList::OVERRIDE_DISABLE_FEATURE);
}

}  // namespace ghost
