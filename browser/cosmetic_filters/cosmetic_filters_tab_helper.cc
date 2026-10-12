// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/cosmetic_filters/cosmetic_filters_tab_helper.h"

#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "ghost/browser/cosmetic_filters/prepared_rules.h"
#include "ghost/common/cosmetic_filters.mojom.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"

namespace ghost::cosmetic_filters {

CosmeticFiltersTabHelper::CosmeticFiltersTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<CosmeticFiltersTabHelper>(*contents) {}

CosmeticFiltersTabHelper::~CosmeticFiltersTabHelper() = default;

void CosmeticFiltersTabHelper::ReadyToCommitNavigation(content::NavigationHandle* navigation) {
  const PreparedRules* rules = PreparedRules::GetForNavigationHandle(*navigation);
  if (!rules) {
    return;
  }
  mojo::AssociatedRemote<mojom::CosmeticFiltersAgent> agent;
  navigation->GetRenderFrameHost()->GetRemoteAssociatedInterfaces()->GetInterface(&agent);
  agent->SetRulesForNextDocument(rules->rules().Clone());
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(CosmeticFiltersTabHelper);

}  // namespace ghost::cosmetic_filters
