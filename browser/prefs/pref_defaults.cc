// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/prefs/pref_defaults.h"

#include "base/values.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/translate/core/browser/translate_pref_names.h"

namespace ghost {

void OverrideProfilePrefDefaults(user_prefs::PrefRegistrySyncable* registry) {
  // Upstream's default (kIncognitoOnly) allows third-party cookies outside
  // Incognito; we block them in every mode.
  registry->SetDefaultPrefValue(
      prefs::kCookieControlsMode,
      base::Value(static_cast<int>(
          content_settings::CookieControlsMode::kBlockThirdParty)));

  // Preloading and preconnect contact sites the user has not chosen to visit.
  registry->SetDefaultPrefValue(
      prefs::kNetworkPredictionOptions,
      base::Value(
          static_cast<int>(prefetch::NetworkPredictionOptions::kDisabled)));

  // Suggestions send what is typed in the omnibox to the search engine as it
  // is typed, before the user decides to search.
  registry->SetDefaultPrefValue(prefs::kSearchSuggestEnabled,
                                base::Value(false));

  // Translation sends page content to Google's translation service; we do
  // not prompt users toward it by default.
  registry->SetDefaultPrefValue(translate::prefs::kOfferTranslateEnabled,
                                base::Value(false));
}

}  // namespace ghost
