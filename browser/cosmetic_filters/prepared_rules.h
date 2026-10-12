// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_COSMETIC_FILTERS_PREPARED_RULES_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_PREPARED_RULES_H_

#include <string>

#include "content/public/browser/navigation_handle_user_data.h"

namespace ghost::cosmetic_filters {

// A navigation's document's rules, from the throttle to the tab helper.
class PreparedRules : public content::NavigationHandleUserData<PreparedRules> {
 public:
  ~PreparedRules() override;

  const std::string& style_sheet() const { return style_sheet_; }
  bool scan() const { return scan_; }

 private:
  friend NavigationHandleUserData;
  PreparedRules(content::NavigationHandle& navigation, std::string style_sheet, bool scan);

  const std::string style_sheet_;
  const bool scan_;

  NAVIGATION_HANDLE_USER_DATA_KEY_DECL();
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_PREPARED_RULES_H_
