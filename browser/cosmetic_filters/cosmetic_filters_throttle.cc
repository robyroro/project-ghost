// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/cosmetic_filters/cosmetic_filters_throttle.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_tab_helper.h"
#include "ghost/browser/cosmetic_filters/prepared_rules.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/components/blocking/blocking_engine.h"
#include "ghost/components/cosmetic_filters/procedural.h"
#include "ghost/components/cosmetic_filters/style_sheet.h"
#include "ghost/components/privacy_policy/protection_level.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ghost::cosmetic_filters {
namespace {

// The URL whose rules the document gets: its own, or for about:blank and
// about:srcdoc, the origin that made it (their content is that site's).
GURL DocumentUrl(content::NavigationHandle& navigation) {
  const GURL& url = navigation.GetURL();
  if (url.SchemeIsHTTPOrHTTPS()) {
    return url;
  }
  if ((url.IsAboutBlank() || url.IsAboutSrcdoc()) && navigation.GetInitiatorOrigin()) {
    return navigation.GetInitiatorOrigin()->GetTupleOrPrecursorTupleIfOpaque().GetURL();
  }
  return GURL();
}

}  // namespace

// static
void CosmeticFiltersThrottle::MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry) {
  CosmeticFiltersTabHelper::CreateForWebContents(registry.GetNavigationHandle().GetWebContents());
  registry.AddThrottle(std::make_unique<CosmeticFiltersThrottle>(registry));
}

CosmeticFiltersThrottle::CosmeticFiltersThrottle(content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

CosmeticFiltersThrottle::~CosmeticFiltersThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult CosmeticFiltersThrottle::WillProcessResponse() {
  return Prepare();
}

content::NavigationThrottle::ThrottleCheckResult
CosmeticFiltersThrottle::WillCommitWithoutUrlLoader() {
  return Prepare();
}

const char* CosmeticFiltersThrottle::GetNameForLogging() {
  return "GhostCosmeticFiltersThrottle";
}

content::NavigationThrottle::ThrottleCheckResult CosmeticFiltersThrottle::Prepare() {
  content::NavigationHandle& navigation = *navigation_handle();
  const GURL document = DocumentUrl(navigation);
  // Browser pages, files, extensions: no rules.
  if (!document.SchemeIsHTTPOrHTTPS()) {
    return PROCEED;
  }
  // The level is the page's: its outermost main frame's (as for requests).
  const GURL page = navigation.IsInOutermostMainFrame()
                        ? document
                        : navigation.GetParentFrameOrOuterDocument()
                              ->GetOutermostMainFrame()
                              ->GetLastCommittedURL();
  if (privacy_policy::GetLevel(navigation.GetWebContents()->GetBrowserContext(), page) ==
      privacy_policy::ProtectionLevel::kOff) {
    PreparedRules::CreateForNavigationHandle(navigation, mojom::DocumentRules::New());
    return PROCEED;
  }
  blocking::BlockingService* service = blocking::BlockingService::GetIfStarted();
  if (!service) {
    return PROCEED;
  }
  service->engine().GetCosmeticRules(
      document, base::BindOnce(&CosmeticFiltersThrottle::OnRules, weak_factory_.GetWeakPtr()));
  return DEFER;
}

void CosmeticFiltersThrottle::OnRules(blocking::CosmeticRules rules) {
  std::vector<StyledSelector> styled;
  for (blocking::StyleRule& rule : rules.style_rules) {
    styled.push_back({std::move(rule.selector), std::move(rule.style)});
  }
  auto document = mojom::DocumentRules::New();
  document->style_sheet =
      base::StrCat({StyleSheetFor(rules.hide_selectors), StyleSheetForActions(styled)});
  document->scan = !rules.generichide;
  document->scriptlet = std::move(rules.scriptlet);
  document->procedural_filters = SafeProceduralFilters(rules.procedural_filters);
  PreparedRules::CreateForNavigationHandle(*navigation_handle(), std::move(document));
  Resume();
}

}  // namespace ghost::cosmetic_filters
