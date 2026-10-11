// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_UI_PROTECTIONS_LEVEL_STRINGS_H_
#define GHOST_BROWSER_UI_PROTECTIONS_LEVEL_STRINGS_H_

#include <string_view>

#include "ghost/components/privacy_policy/protection_level.h"

// What each level is called and does, English only: shared by the protections
// panel and Shade's section of chrome://settings, so the two never disagree.
namespace ghost::protections::strings {

// "Off", "Standard", "Strict".
std::string_view LevelName(privacy_policy::ProtectionLevel level);

// What the level does, in sentences.
std::string_view LevelDetails(privacy_policy::ProtectionLevel level);

}  // namespace ghost::protections::strings

#endif  // GHOST_BROWSER_UI_PROTECTIONS_LEVEL_STRINGS_H_
