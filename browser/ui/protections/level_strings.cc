// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/protections/level_strings.h"

namespace ghost::protections::strings {

using privacy_policy::ProtectionLevel;

std::string_view LevelName(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::kOff:
      return "Off";
    case ProtectionLevel::kStandard:
      return "Standard";
    case ProtectionLevel::kStrict:
      return "Strict";
  }
}

std::string_view LevelDetails(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::kOff:
      return "Trackers and ads load. Links keep their tracking parameters. Cookies, HTTPS and "
             "GPC protections still apply.";
    case ProtectionLevel::kStandard:
      return "Blocks third-party trackers and ads. Removes click identifiers from links to this "
             "site.";
    case ProtectionLevel::kStrict:
      return "Also blocks the site's own trackers. Removes campaign parameters. Sends no "
             "referrer to other sites.";
  }
}

}  // namespace ghost::protections::strings
