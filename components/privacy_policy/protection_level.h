// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_PRIVACY_POLICY_PROTECTION_LEVEL_H_
#define GHOST_COMPONENTS_PRIVACY_POLICY_PROTECTION_LEVEL_H_

#include <optional>
#include <string_view>

namespace ghost::privacy_policy {

// How much a site is protected (docs/privacy-model.md#blocking): Off when it
// broke and the user turned protections off for it; Standard, a regular
// profile's default; Strict, Incognito's.
enum class ProtectionLevel { kOff, kStandard, kStrict };

// The level as the per-site pref stores it: "off", "standard", "strict".
std::string_view ToPrefString(ProtectionLevel level);
std::optional<ProtectionLevel> ParseProtectionLevel(std::string_view text);

}  // namespace ghost::privacy_policy

#endif  // GHOST_COMPONENTS_PRIVACY_POLICY_PROTECTION_LEVEL_H_
