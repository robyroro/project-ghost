// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_COSMETIC_FILTERS_PREPARED_RULES_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_PREPARED_RULES_H_

#include "content/public/browser/navigation_handle_user_data.h"
#include "ghost/common/cosmetic_filters.mojom.h"

namespace ghost::cosmetic_filters {

// A navigation's document's rules, from the throttle to the tab helper.
class PreparedRules : public content::NavigationHandleUserData<PreparedRules> {
 public:
  ~PreparedRules() override;

  const mojom::DocumentRules& rules() const { return *rules_; }

 private:
  friend NavigationHandleUserData;
  PreparedRules(content::NavigationHandle& navigation, mojom::DocumentRulesPtr rules);

  const mojom::DocumentRulesPtr rules_;

  NAVIGATION_HANDLE_USER_DATA_KEY_DECL();
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_PREPARED_RULES_H_
