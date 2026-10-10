// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_PRIVACY_POLICY_EFFECTIVE_POLICY_H_
#define GHOST_COMPONENTS_PRIVACY_POLICY_EFFECTIVE_POLICY_H_

#include <optional>

#include "ghost/components/privacy_policy/protection_level.h"

namespace ghost::privacy_policy {

// What every enforcement point reads for a page: one policy, from the page's
// protection level. GPC, third-party cookies and HTTPS-First aren't here: no
// level changes them (they don't break sites).
struct EffectivePolicy {
  // The blocking engine checks the page's requests (3A).
  bool block_requests = true;
  // ...including requests to the page's own site (Strict).
  bool check_same_site = false;
  // Navigations to the page lose click identifiers (3B)...
  bool strip_click_identifiers = true;
  // ...and campaign parameters.
  bool strip_campaign_parameters = false;
  // Cross-site requests from the page carry a Referer.
  bool cross_site_referrer = true;
};

// The policy a level gives. |campaign_pref| is the profile's choice to strip
// campaign parameters at Standard (3B's pref).
EffectivePolicy PolicyFor(ProtectionLevel level, bool campaign_pref);

// A site's choice, when the user made one, else the mode's default.
ProtectionLevel ResolveLevel(ProtectionLevel mode_default,
                             std::optional<ProtectionLevel> site_override);

}  // namespace ghost::privacy_policy

#endif  // GHOST_COMPONENTS_PRIVACY_POLICY_EFFECTIVE_POLICY_H_
