// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/prefs/profile_prefs.h"

#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/query_filter/prefs.h"

namespace ghost {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  privacy_policy::RegisterProfilePrefs(registry);
  query_filter::RegisterProfilePrefs(registry);
}

}  // namespace ghost
