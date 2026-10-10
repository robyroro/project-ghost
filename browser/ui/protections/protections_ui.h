// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_UI_H_
#define GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_UI_H_

#include <memory>
#include <string_view>

#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "ghost/browser/ui/protections/protections.mojom.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace content {
class BrowserContext;
}

namespace ghost::protections {

inline constexpr char kProtectionsHost[] = "protections.top-chrome";
inline constexpr char kProtectionsURL[] = "chrome://protections.top-chrome/";

class ProtectionsUI;

class ProtectionsUIConfig : public DefaultTopChromeWebUIConfig<ProtectionsUI> {
 public:
  ProtectionsUIConfig();

  // DefaultTopChromeWebUIConfig:
  bool ShouldAutoResizeHost() override;
};

// The protections panel, shown in a bubble from the toolbar's shield.
class ProtectionsUI : public TopChromeWebUIController, public mojom::PageHandler {
 public:
  explicit ProtectionsUI(content::WebUI* web_ui);
  ProtectionsUI(const ProtectionsUI&) = delete;
  ProtectionsUI& operator=(const ProtectionsUI&) = delete;
  ~ProtectionsUI() override;

  static constexpr std::string_view GetWebUIName() { return "Protections"; }

  void BindInterface(mojo::PendingReceiver<mojom::PageHandler> receiver);

  // mojom::PageHandler:
  void ShowUI() override;

 private:
  mojo::Receiver<mojom::PageHandler> receiver_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

// Registers the panel's Mojo interfaces; called from
// PopulateChromeWebUIFrameBinders (patches/0036).
void PopulateWebUIFrameBinders(mojo::BinderMapWithContext<content::RenderFrameHost*>* map);

}  // namespace ghost::protections

#endif  // GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_UI_H_
