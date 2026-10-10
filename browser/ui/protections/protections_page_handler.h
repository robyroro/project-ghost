// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_PAGE_HANDLER_H_
#define GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_PAGE_HANDLER_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "content/public/browser/web_contents_observer.h"
#include "ghost/browser/protections/page_protections.h"
#include "ghost/browser/ui/protections/protections.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace content {
class WebContents;
class WebUI;
}  // namespace content

namespace ghost::protections {

class ProtectionsUI;

// Answers the panel's page for the tab it follows: the active tab of the
// panel's browser when the page last asked (WebUIBubbleManager closes the
// panel when another tab becomes active), and tells the page when that tab's
// state changes.
class ProtectionsPageHandler : public mojom::PageHandler,
                               public PageProtections::Observer,
                               public content::WebContentsObserver {
 public:
  ProtectionsPageHandler(mojo::PendingReceiver<mojom::PageHandler> receiver,
                         mojo::PendingRemote<mojom::Page> page,
                         content::WebUI* web_ui,
                         ProtectionsUI* ui);
  ProtectionsPageHandler(const ProtectionsPageHandler&) = delete;
  ProtectionsPageHandler& operator=(const ProtectionsPageHandler&) = delete;
  ~ProtectionsPageHandler() override;

  // mojom::PageHandler:
  void GetState(GetStateCallback callback) override;
  void SetLevel(mojom::Level level) override;
  void ShowUI() override;

 private:
  // PageProtections::Observer: also a new page, which resets the count.
  void OnBlockedCountChanged() override;

  // content::WebContentsObserver, on the followed tab:
  void WebContentsDestroyed() override;

  // The browser's active tab, or null when the panel has no browser yet (a
  // preloaded page).
  content::WebContents* ActiveTab();
  void Follow(content::WebContents* tab);
  mojom::StatePtr State();
  void SendState();

  mojo::Receiver<mojom::PageHandler> receiver_;
  mojo::Remote<mojom::Page> page_;
  const raw_ptr<content::WebUI> web_ui_;
  const raw_ptr<ProtectionsUI> ui_;
  base::ScopedObservation<PageProtections, PageProtections::Observer> protections_observation_{
      this};
};

}  // namespace ghost::protections

#endif  // GHOST_BROWSER_UI_PROTECTIONS_PROTECTIONS_PAGE_HANDLER_H_
