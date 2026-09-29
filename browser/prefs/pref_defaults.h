// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_PREFS_PREF_DEFAULTS_H_
#define GHOST_BROWSER_PREFS_PREF_DEFAULTS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace ghost {

// Replaces upstream default values of profile prefs with the privacy
// defaults in docs/privacy-model.md. Must run after chrome's
// RegisterProfilePrefs() has registered every pref, because
// SetDefaultPrefValue() only changes prefs that already exist.
//
// Only prefs whose default value is actually consulted belong here. Several
// upstream prefs are ignored until a user or policy sets them explicitly
// (kHttpsFirstBalancedMode is read only after HasPrefPath(); kEnableMediaRouter
// only when policy-managed). Changing their defaults would have no effect, so
// they are controlled through feature overrides instead.
void OverrideProfilePrefDefaults(user_prefs::PrefRegistrySyncable* registry);

}  // namespace ghost

#endif  // GHOST_BROWSER_PREFS_PREF_DEFAULTS_H_
