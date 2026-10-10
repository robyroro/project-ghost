// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/privacy_policy/protection_level.h"

namespace ghost::privacy_policy {

std::string_view ToPrefString(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::kOff:
      return "off";
    case ProtectionLevel::kStandard:
      return "standard";
    case ProtectionLevel::kStrict:
      return "strict";
  }
}

std::optional<ProtectionLevel> ParseProtectionLevel(std::string_view text) {
  for (ProtectionLevel level :
       {ProtectionLevel::kOff, ProtectionLevel::kStandard, ProtectionLevel::kStrict}) {
    if (text == ToPrefString(level)) {
      return level;
    }
  }
  return std::nullopt;
}

}  // namespace ghost::privacy_policy
