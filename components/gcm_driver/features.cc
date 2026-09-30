// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/gcm_driver/features.h"

namespace ghost {

// Feature names share one namespace with upstream's; the prefix keeps a future
// upstream "GoogleCloudMessaging" feature from colliding with ours.
BASE_FEATURE(kGoogleCloudMessaging,
             "GhostGoogleCloudMessaging",
             base::FEATURE_DISABLED_BY_DEFAULT);

}  // namespace ghost
