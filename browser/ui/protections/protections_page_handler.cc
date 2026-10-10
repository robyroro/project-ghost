// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/protections/protections_page_handler.h"

#include <utility>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "ghost/browser/protections/panel_state.h"
#include "ghost/browser/ui/protections/protections_ui.h"

namespace ghost::protections {

namespace {

using privacy_policy::ProtectionLevel;

mojom::Level ToMojo(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::kOff:
      return mojom::Level::kOff;
    case ProtectionLevel::kStandard:
      return mojom::Level::kStandard;
    case ProtectionLevel::kStrict:
      return mojom::Level::kStrict;
  }
}

ProtectionLevel FromMojo(mojom::Level level) {
  switch (level) {
    case mojom::Level::kOff:
      return ProtectionLevel::kOff;
    case mojom::Level::kStandard:
      return ProtectionLevel::kStandard;
    case mojom::Level::kStrict:
      return ProtectionLevel::kStrict;
  }
}

}  // namespace

ProtectionsPageHandler::ProtectionsPageHandler(mojo::PendingReceiver<mojom::PageHandler> receiver,
                                               mojo::PendingRemote<mojom::Page> page,
                                               content::WebUI* web_ui,
                                               ProtectionsUI* ui)
    : receiver_(this, std::move(receiver)), page_(std::move(page)), web_ui_(web_ui), ui_(ui) {}

ProtectionsPageHandler::~ProtectionsPageHandler() = default;

void ProtectionsPageHandler::GetState(GetStateCallback callback) {
  Follow(ActiveTab());
  std::move(callback).Run(State());
}

void ProtectionsPageHandler::SetLevel(mojom::Level level) {
  if (!web_contents()) {
    return;
  }
  ChooseLevel(*web_contents(), FromMojo(level));
  SendState();
}

void ProtectionsPageHandler::ShowUI() {
  ui_->ShowUI();
}

void ProtectionsPageHandler::OnBlockedCountChanged() {
  SendState();
}

void ProtectionsPageHandler::WebContentsDestroyed() {
  // Before the tab's PageProtections goes with it.
  protections_observation_.Reset();
  Observe(nullptr);
}

content::WebContents* ProtectionsPageHandler::ActiveTab() {
  BrowserWindowInterface* browser = webui::GetBrowserWindowInterface(web_ui_->GetWebContents());
  return browser ? browser->GetTabStripModel()->GetActiveWebContents() : nullptr;
}

void ProtectionsPageHandler::Follow(content::WebContents* tab) {
  if (tab == web_contents()) {
    return;
  }
  protections_observation_.Reset();
  Observe(tab);
  if (tab) {
    PageProtections::CreateForWebContents(tab);
    protections_observation_.Observe(PageProtections::FromWebContents(tab));
  }
}

mojom::StatePtr ProtectionsPageHandler::State() {
  auto state = mojom::State::New();
  if (!web_contents()) {
    state->level = mojom::Level::kStandard;
    state->mode_default = mojom::Level::kStandard;
    return state;
  }
  PanelState panel = GetPanelState(*web_contents());
  state->applies = panel.applies;
  state->site = panel.site;
  state->level = ToMojo(panel.level);
  state->mode_default = ToMojo(panel.mode_default);
  state->blocked_count = panel.blocked_count;
  state->off_the_record = panel.off_the_record;
  return state;
}

void ProtectionsPageHandler::SendState() {
  page_->OnStateChanged(State());
}

}  // namespace ghost::protections
