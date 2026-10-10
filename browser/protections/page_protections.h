// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_PROTECTIONS_PAGE_PROTECTIONS_H_
#define GHOST_BROWSER_PROTECTIONS_PAGE_PROTECTIONS_H_

#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace ghost::protections {

// The requests and connections Shade blocked for the primary page of a tab,
// its subframes and dedicated workers included (docs/privacy-model.md). A new
// primary page starts from zero. Shared and service workers aren't counted:
// no tab is theirs.
class PageProtections : public content::WebContentsObserver,
                        public content::WebContentsUserData<PageProtections> {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnBlockedCountChanged() = 0;
  };

  PageProtections(const PageProtections&) = delete;
  PageProtections& operator=(const PageProtections&) = delete;
  ~PageProtections() override;

  // Counts one block for the tab whose primary page |frame| belongs to;
  // nothing when the frame is gone or its page isn't primary (a page being
  // left, a prerendered one).
  static void RecordBlocked(content::GlobalRenderFrameHostId frame);

  int blocked_count() const { return blocked_count_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  friend class content::WebContentsUserData<PageProtections>;

  explicit PageProtections(content::WebContents* contents);

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

  void SetCount(int count);

  int blocked_count_ = 0;
  base::ObserverList<Observer> observers_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace ghost::protections

#endif  // GHOST_BROWSER_PROTECTIONS_PAGE_PROTECTIONS_H_
