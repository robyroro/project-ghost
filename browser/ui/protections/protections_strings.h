// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_STRINGS_H_
#define GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_STRINGS_H_

#include <string>

#include "ghost/components/privacy_policy/protection_level.h"

namespace content {
class WebUIDataSource;
}

// The protections panel's and button's strings, English only. A grd with
// translations replaces this file when Shade's strings are translated; a grd
// of messages now would need its own locale paks (another patch) for none.
namespace ghost::protections::strings {

// The page's strings, under the names its code reads with loadTimeData.
void AddToDataSource(content::WebUIDataSource& source);

// "Protections: Standard, 3 requests blocked", "Protections: Off".
std::u16string ButtonAccessibleName(privacy_policy::ProtectionLevel level, int blocked_count);

// The disabled button's tooltip.
std::u16string NotApplicable();

}  // namespace ghost::protections::strings

#endif  // GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_STRINGS_H_
