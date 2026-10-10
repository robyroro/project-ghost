// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_BUTTON_H_
#define GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_BUTTON_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "content/public/browser/web_contents_observer.h"
#include "ghost/browser/protections/page_protections.h"
#include "ghost/browser/protections/panel_state.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_header_macros.h"

class BrowserWindowInterface;
class WebUIBubbleManager;

namespace ghost::protections {

DECLARE_ELEMENT_IDENTIFIER_VALUE(kProtectionsButtonElementId);

// The shield right of the omnibox. It follows the active tab: the count of
// requests blocked on its page as a badge, a struck shield when the site is
// Off, disabled where protections don't apply. A click opens the panel.
class ProtectionsButton : public ToolbarButton,
                          public TabStripModelObserver,
                          public PageProtections::Observer,
                          public content::WebContentsObserver {
  METADATA_HEADER(ProtectionsButton, ToolbarButton)

 public:
  explicit ProtectionsButton(BrowserWindowInterface* browser);
  ProtectionsButton(const ProtectionsButton&) = delete;
  ProtectionsButton& operator=(const ProtectionsButton&) = delete;
  ~ProtectionsButton() override;

  WebUIBubbleManager* bubble_manager_for_testing() { return bubble_manager_.get(); }
  // The badge's text: empty without one.
  const std::u16string& badge_text_for_testing() const { return badge_text_; }
  bool shows_off_for_testing() const { return state_.applies && IsOff(); }

  // ToolbarButton:
  void UpdateIcon() override;

 private:
  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;

  // PageProtections::Observer: a block, or a new page (which also follows a
  // level's change: the page reloads).
  void OnBlockedCountChanged() override;

  // content::WebContentsObserver, on the active tab:
  void WebContentsDestroyed() override;

  void Follow(content::WebContents* tab);
  void Update();
  bool IsOff() const;
  void ShowPanel();

  const raw_ptr<BrowserWindowInterface> browser_;
  std::unique_ptr<WebUIBubbleManager> bubble_manager_;
  base::ScopedObservation<PageProtections, PageProtections::Observer> protections_observation_{
      this};
  PanelState state_;
  std::u16string badge_text_;
};

// For ToolbarView::Init (patches/0035).
std::unique_ptr<views::View> CreateProtectionsButton(BrowserWindowInterface* browser);

}  // namespace ghost::protections

#endif  // GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_BUTTON_H_
