// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_PROTECTIONS_PANEL_STATE_H_
#define GHOST_BROWSER_PROTECTIONS_PANEL_STATE_H_

#include <string>

#include "ghost/components/privacy_policy/protection_level.h"

namespace content {
class WebContents;
}

namespace ghost::protections {

// What the protections panel and its button show for a tab's page.
struct PanelState {
  // Whether protections apply: an http(s) page.
  bool applies = false;
  // The page's registrable domain, which a level is chosen for.
  std::string site;
  privacy_policy::ProtectionLevel level = privacy_policy::ProtectionLevel::kStandard;
  // The profile's mode's level: Strict off the record, Standard otherwise.
  privacy_policy::ProtectionLevel mode_default = privacy_policy::ProtectionLevel::kStandard;
  // Requests and connections blocked on the page (PageProtections).
  int blocked_count = 0;
  bool off_the_record = false;
};

PanelState GetPanelState(content::WebContents& contents);

// Sets the page's site to |level|, or clears its choice when |level| is the
// mode's default, so that a default changed later applies to it; then reloads
// the page, where the level takes effect. Nothing where protections don't
// apply.
void ChooseLevel(content::WebContents& contents, privacy_policy::ProtectionLevel level);

}  // namespace ghost::protections

#endif  // GHOST_BROWSER_PROTECTIONS_PANEL_STATE_H_
