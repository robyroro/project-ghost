// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/privacy_policy/site_levels.h"

#include <string>

#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "ghost/browser/query_filter/prefs.h"
#include "ghost/components/site/registrable_domain.h"
#include "url/gurl.h"

namespace ghost::privacy_policy {
namespace {

// The dictionary's key for a page: its site, or nothing for a page without
// a host (about:blank, a data: URL).
std::string SiteKey(const GURL& page) {
  if (!page.is_valid() || !page.has_host()) {
    return std::string();
  }
  return std::string(RegistrableDomain(page.host()));
}

PrefService* Prefs(content::BrowserContext* context) {
  return user_prefs::UserPrefs::Get(context);
}

std::optional<ProtectionLevel> SiteLevel(content::BrowserContext* context, const GURL& page) {
  const std::string key = SiteKey(page);
  if (key.empty()) {
    return std::nullopt;
  }
  const std::string* stored = Prefs(context)->GetDict(kSiteLevelsPref).FindString(key);
  return stored ? ParseProtectionLevel(*stored) : std::nullopt;
}

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(kSiteLevelsPref);
}

ProtectionLevel ModeDefault(content::BrowserContext* context) {
  return context->IsOffTheRecord() ? ProtectionLevel::kStrict : ProtectionLevel::kStandard;
}

ProtectionLevel GetLevel(content::BrowserContext* context, const GURL& page) {
  return ResolveLevel(ModeDefault(context), SiteLevel(context, page));
}

void SetLevel(content::BrowserContext* context, const GURL& page, ProtectionLevel level) {
  const std::string key = SiteKey(page);
  if (!key.empty()) {
    ScopedDictPrefUpdate(Prefs(context), kSiteLevelsPref)->Set(key, ToPrefString(level));
  }
}

void ClearLevel(content::BrowserContext* context, const GURL& page) {
  const std::string key = SiteKey(page);
  if (!key.empty()) {
    ScopedDictPrefUpdate(Prefs(context), kSiteLevelsPref)->Remove(key);
  }
}

EffectivePolicy GetPolicy(content::BrowserContext* context, const GURL& page) {
  return PolicyFor(GetLevel(context, page),
                   Prefs(context)->GetBoolean(query_filter::kStripCampaignParametersPref));
}

}  // namespace ghost::privacy_policy
