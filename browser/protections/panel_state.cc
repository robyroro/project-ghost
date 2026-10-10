// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/panel_state.h"

#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/protections/page_protections.h"
#include "ghost/components/site/registrable_domain.h"
#include "url/gurl.h"

namespace ghost::protections {

namespace {

GURL PageOf(content::WebContents& contents) {
  return contents.GetPrimaryMainFrame()->GetLastCommittedURL();
}

}  // namespace

PanelState GetPanelState(content::WebContents& contents) {
  content::BrowserContext* context = contents.GetBrowserContext();
  const GURL page = PageOf(contents);
  PanelState state;
  state.applies = page.SchemeIsHTTPOrHTTPS();
  state.off_the_record = context->IsOffTheRecord();
  state.mode_default = privacy_policy::ModeDefault(context);
  state.level = state.mode_default;
  if (state.applies) {
    state.site = std::string(RegistrableDomain(page.host()));
    state.level = privacy_policy::GetLevel(context, page);
  }
  if (PageProtections* protections = PageProtections::FromWebContents(&contents)) {
    state.blocked_count = protections->blocked_count();
  }
  return state;
}

void ChooseLevel(content::WebContents& contents, privacy_policy::ProtectionLevel level) {
  content::BrowserContext* context = contents.GetBrowserContext();
  const GURL page = PageOf(contents);
  if (!page.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  if (level == privacy_policy::ModeDefault(context)) {
    privacy_policy::ClearLevel(context, page);
  } else {
    privacy_policy::SetLevel(context, page, level);
  }
  contents.GetController().Reload(content::ReloadType::NORMAL, /*check_for_repost=*/true);
}

}  // namespace ghost::protections
