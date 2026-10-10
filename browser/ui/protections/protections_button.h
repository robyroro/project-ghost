// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_BUTTON_H_
#define GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_BUTTON_H_

#include <memory>

#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_header_macros.h"

class BrowserWindowInterface;
class WebUIBubbleManager;

namespace ghost::protections {

DECLARE_ELEMENT_IDENTIFIER_VALUE(kProtectionsButtonElementId);

// The shield right of the omnibox: opens the protections panel.
class ProtectionsButton : public ToolbarButton {
  METADATA_HEADER(ProtectionsButton, ToolbarButton)

 public:
  explicit ProtectionsButton(BrowserWindowInterface* browser);
  ProtectionsButton(const ProtectionsButton&) = delete;
  ProtectionsButton& operator=(const ProtectionsButton&) = delete;
  ~ProtectionsButton() override;

  WebUIBubbleManager* bubble_manager_for_testing() { return bubble_manager_.get(); }

 private:
  void ShowPanel();

  std::unique_ptr<WebUIBubbleManager> bubble_manager_;
};

// For ToolbarView::Init (patches/0035).
std::unique_ptr<views::View> CreateProtectionsButton(BrowserWindowInterface* browser);

}  // namespace ghost::protections

#endif  // GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_BUTTON_H_
