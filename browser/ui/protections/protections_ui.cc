// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/protections/protections_ui.h"

#include <utility>

#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller_interface_binder.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "ghost/grit/protections_resources.h"
#include "ghost/grit/protections_resources_map.h"
#include "ui/webui/webui_util.h"

namespace ghost::protections {

ProtectionsUIConfig::ProtectionsUIConfig()
    : DefaultTopChromeWebUIConfig(content::kChromeUIScheme, kProtectionsHost) {}

bool ProtectionsUIConfig::ShouldAutoResizeHost() {
  return true;
}

ProtectionsUI::ProtectionsUI(content::WebUI* web_ui) : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(), kProtectionsHost);
  webui::SetupWebUIDataSource(source, kProtectionsResources, IDR_PROTECTIONS_PROTECTIONS_HTML);
}

ProtectionsUI::~ProtectionsUI() = default;

void ProtectionsUI::BindInterface(mojo::PendingReceiver<mojom::PageHandler> receiver) {
  receiver_.reset();
  receiver_.Bind(std::move(receiver));
}

void ProtectionsUI::ShowUI() {
  if (embedder()) {
    embedder()->ShowUI();
  }
}

WEB_UI_CONTROLLER_TYPE_IMPL(ProtectionsUI)

void PopulateWebUIFrameBinders(mojo::BinderMapWithContext<content::RenderFrameHost*>* map) {
  content::RegisterWebUIControllerInterfaceBinder<mojom::PageHandler, ProtectionsUI>(map);
}

}  // namespace ghost::protections
