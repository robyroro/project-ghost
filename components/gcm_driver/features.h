// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_GCM_DRIVER_FEATURES_H_
#define GHOST_COMPONENTS_GCM_DRIVER_FEATURES_H_

#include "base/feature_list.h"

namespace ghost {

// Google Cloud Messaging: Chromium's push channel, which checks in with
// android.clients.google.com and keeps a connection open to mtalk.google.com.
// Web Push and Chrome's own push-based features depend on it. Disabled by
// default; GCMDriverDesktop does not start while it is off (patches/0006).
// Developers can enable it with --enable-features=GhostGoogleCloudMessaging
// to test sites that use Web Push.
BASE_DECLARE_FEATURE(kGoogleCloudMessaging);

}  // namespace ghost

#endif  // GHOST_COMPONENTS_GCM_DRIVER_FEATURES_H_
