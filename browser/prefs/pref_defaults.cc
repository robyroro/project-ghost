// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/prefs/pref_defaults.h"

#include "base/values.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "extensions/browser/pref_names.h"
#include "third_party/blink/public/common/peerconnection/webrtc_ip_handling_policy.h"

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

  // WebRTC uses only the interface the system routes through by default, so
  // it reveals no other address (a VPN's physical interface, other networks).
  registry->SetDefaultPrefValue(
      prefs::kWebRTCIPHandlingPolicy,
      base::Value(blink::kWebRTCIPHandlingDefaultPublicInterfaceOnly));

  // Suggestions send what is typed in the omnibox to the search engine as it
  // is typed, before the user decides to search.
  registry->SetDefaultPrefValue(prefs::kSearchSuggestEnabled,
                                base::Value(false));

  // Translation sends page content to Google's translation service; we do
  // not prompt users toward it by default.
  registry->SetDefaultPrefValue(translate::prefs::kOfferTranslateEnabled,
                                base::Value(false));

  // Safe Browsing downloads threat lists from Google and, depending on the
  // mode, sends URL and download metadata to it. It stays off until the
  // release gate in docs/licensing.md decides between Google's service, a
  // privacy-preserving proxy, or none. Turning it off in every profile also
  // stops the list updates, which run while any profile has it on.
  registry->SetDefaultPrefValue(prefs::kSafeBrowsingEnabled,
                                base::Value(false));

  // After every sign-in to a site, the password leak check sends Google a
  // hash prefix of the username and an encrypted hash of the username and
  // password, and learns when and from which address the user signed in.
  // Upstream runs it without Safe Browsing and without a Google account.
  registry->SetDefaultPrefValue(
      password_manager::prefs::kPasswordLeakDetectionEnabled,
      base::Value(false));

  // Other programs install extensions into every Chromium-based browser by
  // writing to HKLM\Software\Google\Chrome\Extensions, which upstream reads
  // whatever the browser's brand. The browser downloads the extension from
  // the Chrome Web Store before asking the user whether to enable it. On
  // Windows this pref removes only that registry source; policy-installed and
  // user-installed extensions are unaffected.
  registry->SetDefaultPrefValue(
      extensions::pref_names::kBlockExternalExtensions, base::Value(true));
}

}  // namespace ghost
