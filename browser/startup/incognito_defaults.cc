// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/startup/incognito_defaults.h"

#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace ghost {

IncognitoDefaults::IncognitoDefaults() = default;

IncognitoDefaults::~IncognitoDefaults() = default;

void IncognitoDefaults::Watch(Profile* profile) {
  if (!profile->IsOffTheRecord() && !observations_.IsObservingSource(profile)) {
    observations_.AddObservation(profile);
  }
}

void IncognitoDefaults::OnOffTheRecordProfileCreated(Profile* off_the_record) {
  if (!off_the_record->IsIncognitoProfile()) {
    return;
  }
  // Strict HTTPS-First: warn before any page over HTTP, intranet names
  // included. Upstream's Incognito warns only as balanced mode does, which
  // Normal already has. The pref isn't on the list of prefs Incognito writes
  // through (pref_service_incognito_allowlist.cc): it stays in memory.
  off_the_record->GetPrefs()->SetBoolean(prefs::kHttpsOnlyModeEnabled, true);
}

void IncognitoDefaults::OnProfileWillBeDestroyed(Profile* profile) {
  if (observations_.IsObservingSource(profile)) {
    observations_.RemoveObservation(profile);
  }
}

}  // namespace ghost
