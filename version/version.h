// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_VERSION_VERSION_H_
#define GHOST_VERSION_VERSION_H_

#include <string>

namespace base {
class Version;
}

namespace ghost {

// The release version: chrome/VERSION, which the installer, the updater and
// Windows also use. In a release build its fourth part is the Chromium
// release's PATCH * 100 + respin (ADR 0007). In a development build it equals
// ChromiumVersion().
const base::Version& ReleaseVersion();

// The Chromium release this build is based on: CHROMIUM_VERSION. version_info
// reports it (patches/0012), so it is the only version websites and Google
// see.
const base::Version& ChromiumVersion();

// The version as people see it: the Chromium release, then "-<respin>" unless
// the respin is 0. For example "152.0.7977.149-1".
std::string DisplayVersion();

// DisplayVersion() for any pair. `release` must equal `chromium` (a
// development build) or be a release of it.
std::string FormatDisplayVersion(const base::Version& release,
                                 const base::Version& chromium);

}  // namespace ghost

#endif  // GHOST_VERSION_VERSION_H_
