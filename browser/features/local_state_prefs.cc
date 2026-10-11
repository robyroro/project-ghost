// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/features/local_state_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace ghost {

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kGpcEnabledPref, true);
}

}  // namespace ghost
