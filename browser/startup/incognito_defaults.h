// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_STARTUP_INCOGNITO_DEFAULTS_H_
#define GHOST_BROWSER_STARTUP_INCOGNITO_DEFAULTS_H_

#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"

namespace ghost {

// Shade's defaults for Incognito, which pref defaults can't express: a pref
// has one default for a regular profile and its Incognito profile alike. They
// are set in each Incognito profile as it is created, in its in-memory prefs,
// so the regular profile and the disk never see them. Incognito becomes
// Private mode (docs/privacy-model.md#modes).
class IncognitoDefaults : public ProfileObserver {
 public:
  IncognitoDefaults();
  IncognitoDefaults(const IncognitoDefaults&) = delete;
  IncognitoDefaults& operator=(const IncognitoDefaults&) = delete;
  ~IncognitoDefaults() override;

  // Watches a regular profile for the Incognito profiles it creates.
  void Watch(Profile* profile);

  // ProfileObserver:
  void OnOffTheRecordProfileCreated(Profile* off_the_record) override;
  void OnProfileWillBeDestroyed(Profile* profile) override;

 private:
  base::ScopedMultiSourceObservation<Profile, ProfileObserver> observations_{this};
};

}  // namespace ghost

#endif  // GHOST_BROWSER_STARTUP_INCOGNITO_DEFAULTS_H_
