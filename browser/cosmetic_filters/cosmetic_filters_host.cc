// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/cosmetic_filters/cosmetic_filters_host.h"

#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/render_frame_host.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/components/cosmetic_filters/names.h"
#include "ghost/components/cosmetic_filters/style_sheet.h"
#include "ghost/components/privacy_policy/protection_level.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ghost::cosmetic_filters {
namespace {

// As the throttle's: the document's URL, or for a document without one of
// its own (about:blank, srcdoc, an initial empty document), its origin's.
GURL DocumentUrl(content::RenderFrameHost& frame) {
  const GURL& url = frame.GetLastCommittedURL();
  if (url.SchemeIsHTTPOrHTTPS()) {
    return url;
  }
  return frame.GetLastCommittedOrigin().GetTupleOrPrecursorTupleIfOpaque().GetURL();
}

}  // namespace

// static
void CosmeticFiltersHost::Create(content::RenderFrameHost* frame,
                                 mojo::PendingReceiver<mojom::CosmeticFiltersHost> receiver) {
  // Deletes itself with the document or the connection (DocumentService).
  new CosmeticFiltersHost(*frame, std::move(receiver));
}

CosmeticFiltersHost::CosmeticFiltersHost(content::RenderFrameHost& frame,
                                         mojo::PendingReceiver<mojom::CosmeticFiltersHost> receiver)
    : DocumentService(frame, std::move(receiver)) {}

CosmeticFiltersHost::~CosmeticFiltersHost() = default;

void CosmeticFiltersHost::GetGenericStyleSheet(const std::vector<std::string>& classes,
                                               const std::vector<std::string>& ids,
                                               GetGenericStyleSheetCallback callback) {
  if (!AreValidNames(classes, ids)) {
    ReportBadMessageAndDeleteThis("CosmeticFiltersHost: names outside the limits");
    return;
  }
  const GURL document = DocumentUrl(render_frame_host());
  blocking::BlockingService* service = blocking::BlockingService::GetIfStarted();
  // A renderer told not to scan may still call: it gets nothing.
  if (!service || !document.SchemeIsHTTPOrHTTPS() ||
      privacy_policy::GetLevel(render_frame_host().GetBrowserContext(),
                               render_frame_host().GetOutermostMainFrame()->GetLastCommittedURL()) ==
          privacy_policy::ProtectionLevel::kOff) {
    std::move(callback).Run(std::string());
    return;
  }
  if (exceptions_) {
    Select(classes, ids, std::move(callback));
    return;
  }
  service->engine().GetCosmeticRules(
      document, base::BindOnce(&CosmeticFiltersHost::OnRules, weak_factory_.GetWeakPtr(),
                               classes, ids, std::move(callback)));
}

void CosmeticFiltersHost::OnRules(std::vector<std::string> classes,
                                  std::vector<std::string> ids,
                                  GetGenericStyleSheetCallback callback,
                                  blocking::CosmeticRules rules) {
  exceptions_ = std::move(rules.exceptions);
  generichide_ = rules.generichide;
  Select(std::move(classes), std::move(ids), std::move(callback));
}

void CosmeticFiltersHost::Select(std::vector<std::string> classes,
                                 std::vector<std::string> ids,
                                 GetGenericStyleSheetCallback callback) {
  blocking::BlockingService* service = blocking::BlockingService::GetIfStarted();
  if (generichide_ || !service) {
    std::move(callback).Run(std::string());
    return;
  }
  service->engine().GetGenericSelectors(
      std::move(classes), std::move(ids), *exceptions_,
      base::BindOnce(
          [](GetGenericStyleSheetCallback callback, std::vector<std::string> selectors) {
            std::move(callback).Run(StyleSheetFor(selectors));
          },
          std::move(callback)));
}

}  // namespace ghost::cosmetic_filters
