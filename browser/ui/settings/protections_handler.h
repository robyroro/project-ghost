// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_UI_SETTINGS_PROTECTIONS_HANDLER_H_
#define GHOST_BROWSER_UI_SETTINGS_PROTECTIONS_HANDLER_H_

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"

class Profile;

namespace ghost::settings {

// chrome://settings' writes to the site list of Shade's section. Reads go
// through settingsPrivate (patches/0040); writes come here so that the level
// and the site are checked, and the site keyed, as the panel does.
class ProtectionsHandler : public ::settings::SettingsPageUIHandler {
 public:
  explicit ProtectionsHandler(Profile* profile);
  ProtectionsHandler(const ProtectionsHandler&) = delete;
  ProtectionsHandler& operator=(const ProtectionsHandler&) = delete;
  ~ProtectionsHandler() override;

  // ::settings::SettingsPageUIHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override {}
  void OnJavascriptDisallowed() override {}

 private:
  // [site, level]: the site is a host, keyed by its registrable domain.
  void HandleSetSiteLevel(const base::ListValue& args);
  // [site]
  void HandleClearSiteLevel(const base::ListValue& args);

  const raw_ptr<Profile> profile_;
};

}  // namespace ghost::settings

#endif  // GHOST_BROWSER_UI_SETTINGS_PROTECTIONS_HANDLER_H_
