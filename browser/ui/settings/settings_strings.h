// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_UI_SETTINGS_SETTINGS_STRINGS_H_
#define GHOST_BROWSER_UI_SETTINGS_SETTINGS_STRINGS_H_

namespace content {
class WebUIDataSource;
}

namespace ghost::settings {

// The strings of Shade's section of chrome://settings, English only, under
// the names its code reads with loadTimeData (prefixed "shade" to stay apart
// from upstream's). A grd with translations replaces this file when Shade's
// strings are translated.
void AddSettingsStrings(content::WebUIDataSource* source);

}  // namespace ghost::settings

#endif  // GHOST_BROWSER_UI_SETTINGS_SETTINGS_STRINGS_H_
