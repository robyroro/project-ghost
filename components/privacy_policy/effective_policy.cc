// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/privacy_policy/effective_policy.h"

namespace ghost::privacy_policy {

EffectivePolicy PolicyFor(ProtectionLevel level, bool campaign_pref) {
  switch (level) {
    case ProtectionLevel::kOff:
      return {.block_requests = false,
              .check_same_site = false,
              .strip_click_identifiers = false,
              .strip_campaign_parameters = false,
              .cross_site_referrer = true};
    case ProtectionLevel::kStandard:
      return {.block_requests = true,
              .check_same_site = false,
              .strip_click_identifiers = true,
              .strip_campaign_parameters = campaign_pref,
              .cross_site_referrer = true};
    case ProtectionLevel::kStrict:
      return {.block_requests = true,
              .check_same_site = true,
              .strip_click_identifiers = true,
              .strip_campaign_parameters = true,
              .cross_site_referrer = false};
  }
}

ProtectionLevel ResolveLevel(ProtectionLevel mode_default,
                             std::optional<ProtectionLevel> site_override) {
  return site_override.value_or(mode_default);
}

}  // namespace ghost::privacy_policy
