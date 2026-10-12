// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TAB_HELPER_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TAB_HELPER_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace ghost::cosmetic_filters {

// Sends each navigation's prepared rules to its frame's agent just before the
// commit, as subresource_filter sends its activation. Created by the throttle
// with a tab's first navigation.
class CosmeticFiltersTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<CosmeticFiltersTabHelper> {
 public:
  CosmeticFiltersTabHelper(const CosmeticFiltersTabHelper&) = delete;
  CosmeticFiltersTabHelper& operator=(const CosmeticFiltersTabHelper&) = delete;
  ~CosmeticFiltersTabHelper() override;

 private:
  friend WebContentsUserData;
  explicit CosmeticFiltersTabHelper(content::WebContents* contents);

  // content::WebContentsObserver:
  void ReadyToCommitNavigation(content::NavigationHandle* navigation) override;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TAB_HELPER_H_
