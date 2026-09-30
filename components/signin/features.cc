// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/signin/features.h"

namespace ghost {

// Prefixed like every Ghost feature, so it cannot collide with an upstream
// feature name.
BASE_FEATURE(kGoogleAccountsInCookieJar,
             "GhostGoogleAccountsInCookieJar",
             base::FEATURE_DISABLED_BY_DEFAULT);

}  // namespace ghost
