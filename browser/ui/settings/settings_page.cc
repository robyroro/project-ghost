// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/settings/settings_page.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/common/buildflags.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ghost/browser/ui/settings/protections_handler.h"

#if !BUILDFLAG(OPTIMIZE_WEBUI)
#include "ghost/grit/shade_settings_resources_map.h"
#endif

namespace ghost::settings {

void SetUpSettingsPage(content::WebUI* web_ui, content::WebUIDataSource* source) {
#if !BUILDFLAG(OPTIMIZE_WEBUI)
  // Bundled into settings.js otherwise, as upstream's settings_shared is.
  source->AddResourcePaths(kShadeSettingsResources);
#endif
  source->AddString("shadeProtectionsTitle", "Shade protections");
  web_ui->AddMessageHandler(std::make_unique<ProtectionsHandler>(Profile::FromWebUI(web_ui)));
}

}  // namespace ghost::settings
