// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_SIGNIN_FEATURES_H_
#define GHOST_COMPONENTS_SIGNIN_FEATURES_H_

#include "base/feature_list.h"

namespace ghost {

// Chromium keeps track of the Google accounts signed in on the web by asking
// accounts.google.com/ListAccounts, with the user's Google cookies. It asks
// whenever anything reads the list while it is stale, which on a fresh profile
// happens at startup, and whenever Google's sign-in cookies change. Browser
// sign-in is already unavailable without Google API keys, so nothing in Ghost
// needs the answer. Disabled by default: GaiaCookieManagerService then never
// sends the request, and the list stays empty (patches/0009).
BASE_DECLARE_FEATURE(kGoogleAccountsInCookieJar);

}  // namespace ghost

#endif  // GHOST_COMPONENTS_SIGNIN_FEATURES_H_
