// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_PRIVACY_POLICY_SITE_LEVELS_H_
#define GHOST_BROWSER_PRIVACY_POLICY_SITE_LEVELS_H_

#include <optional>

#include "ghost/components/privacy_policy/effective_policy.h"
#include "ghost/components/privacy_policy/protection_level.h"

class GURL;

namespace content {
class BrowserContext;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace ghost::privacy_policy {

// The user's choice per site: a dictionary from registrable domain to a
// level's pref string. A site without an entry has its mode's default. Not
// synced. Incognito reads the regular profile's dictionary until it writes
// its own, which stays in memory (the pref isn't on Incognito's write-through
// list).
inline constexpr char kSiteLevelsPref[] = "ghost.privacy_policy.site_levels";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// Strict off the record (Incognito, which becomes Private mode), Standard
// otherwise (docs/privacy-model.md#modes).
ProtectionLevel ModeDefault(content::BrowserContext* context);

// The level for a page: its site's (the registrable domain of its host), or
// the mode's default.
ProtectionLevel GetLevel(content::BrowserContext* context, const GURL& page);
void SetLevel(content::BrowserContext* context, const GURL& page, ProtectionLevel level);
void ClearLevel(content::BrowserContext* context, const GURL& page);

// What the enforcement points read for a page.
EffectivePolicy GetPolicy(content::BrowserContext* context, const GURL& page);

}  // namespace ghost::privacy_policy

#endif  // GHOST_BROWSER_PRIVACY_POLICY_SITE_LEVELS_H_
