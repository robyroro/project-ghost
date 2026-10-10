// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/query_filter/prefs.h"

#include "components/pref_registry/pref_registry_syncable.h"

namespace ghost::query_filter {

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kStripCampaignParametersPref, false);
}

}  // namespace ghost::query_filter
