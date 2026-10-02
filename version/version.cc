// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/version/version.h"

#include <stdint.h>

#include <string>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/version.h"
#include "ghost/version/version_values.h"

namespace ghost {

const base::Version& ReleaseVersion() {
  static const base::NoDestructor<base::Version> version(GHOST_RELEASE_VERSION);
  return *version;
}

const base::Version& ChromiumVersion() {
  static const base::NoDestructor<base::Version> version(
      GHOST_CHROMIUM_VERSION);
  return *version;
}

std::string FormatDisplayVersion(const base::Version& release,
                                 const base::Version& chromium) {
  const std::vector<uint32_t>& r = release.components();
  const std::vector<uint32_t>& c = chromium.components();
  CHECK_EQ(r.size(), 4u);
  CHECK_EQ(c.size(), 4u);
  CHECK(r[0] == c[0] && r[1] == c[1] && r[2] == c[2])
      << release << " is not a release of " << chromium;
  if (r[3] == c[3]) {
    return chromium.GetString();  // A development build.
  }
  CHECK_GE(r[3], c[3] * 100) << release << " is not a release of " << chromium;
  const uint32_t respin = r[3] - c[3] * 100;
  CHECK_LT(respin, 100u) << release << " is not a release of " << chromium;
  if (respin == 0) {
    return chromium.GetString();
  }
  return chromium.GetString() + "-" + base::NumberToString(respin);
}

std::string DisplayVersion() {
  return FormatDisplayVersion(ReleaseVersion(), ChromiumVersion());
}

}  // namespace ghost
