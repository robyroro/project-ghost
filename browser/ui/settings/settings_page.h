// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_UI_SETTINGS_SETTINGS_PAGE_H_
#define GHOST_BROWSER_UI_SETTINGS_SETTINGS_PAGE_H_

namespace content {
class WebUI;
class WebUIDataSource;
}  // namespace content

namespace ghost::settings {

// Called by chrome://settings' SettingsUI (patches/0038) once its data source
// has upstream's strings: adds Shade's section's strings, its /shade/
// resources (bundled into settings.js when optimize_webui), and its message
// handler.
void SetUpSettingsPage(content::WebUI* web_ui, content::WebUIDataSource* source);

}  // namespace ghost::settings

#endif  // GHOST_BROWSER_UI_SETTINGS_SETTINGS_PAGE_H_
