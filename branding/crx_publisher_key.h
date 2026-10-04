// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The publisher key hashes of the build's signing identity:
// branding/keys/<identity>.h, chosen by ghost_signing_identity
// (branding/signing.gni).

#ifndef GHOST_BRANDING_CRX_PUBLISHER_KEY_H_
#define GHOST_BRANDING_CRX_PUBLISHER_KEY_H_

#include "ghost/branding/signing_buildflags.h"

#if BUILDFLAG(GHOST_SIGNING_IDENTITY_TEST)
#include "ghost/branding/keys/test.h"  // IWYU pragma: export
#else
#include "ghost/branding/keys/dev.h"  // IWYU pragma: export
#endif

#endif  // GHOST_BRANDING_CRX_PUBLISHER_KEY_H_
