// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/protections/protections_button.h"

#include "base/functional/bind.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_manager.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ghost/browser/ui/protections/protections_ui.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"

namespace ghost::protections {

DEFINE_ELEMENT_IDENTIFIER_VALUE(kProtectionsButtonElementId);

ProtectionsButton::ProtectionsButton(BrowserWindowInterface* browser)
    : ToolbarButton(base::BindRepeating(&ProtectionsButton::ShowPanel, base::Unretained(this))),
      // The task manager names the panel's process with an upstream string
      // until Shade's strings have a grd (progress notes, 3D-3).
      bubble_manager_(WebUIBubbleManager::Create<ProtectionsUI>(
          browser, GURL(kProtectionsURL), IDS_SETTINGS_PRIVACY)) {
  SetProperty(views::kElementIdentifierKey, kProtectionsButtonElementId);
  SetVectorIcon(vector_icons::kShieldIcon);
  GetViewAccessibility().SetName(u"Protections");
}

ProtectionsButton::~ProtectionsButton() = default;

void ProtectionsButton::ShowPanel() {
  bubble_manager_->ShowBubble(this);
}

BEGIN_METADATA(ProtectionsButton)
END_METADATA

std::unique_ptr<views::View> CreateProtectionsButton(BrowserWindowInterface* browser) {
  return std::make_unique<ProtectionsButton>(browser);
}

}  // namespace ghost::protections
