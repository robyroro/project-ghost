// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_PREFS_PROFILE_PREFS_H_
#define GHOST_BROWSER_PREFS_PROFILE_PREFS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace ghost {

// Registers Ghost's own profile prefs; chrome's RegisterProfilePrefs() calls
// it (patches/0032).
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

}  // namespace ghost

#endif  // GHOST_BROWSER_PREFS_PROFILE_PREFS_H_
