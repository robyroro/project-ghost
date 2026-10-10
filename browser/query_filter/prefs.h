// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_QUERY_FILTER_PREFS_H_
#define GHOST_BROWSER_QUERY_FILTER_PREFS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace ghost::query_filter {

// Whether a regular profile strips campaign parameters (utm_*) too; Incognito
// always does. Off by default (docs/privacy-model.md#tracking-parameters);
// its switch comes with 3D. Not synced.
inline constexpr char kStripCampaignParametersPref[] =
    "ghost.query_filter.strip_campaign_parameters";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

}  // namespace ghost::query_filter

#endif  // GHOST_BROWSER_QUERY_FILTER_PREFS_H_
