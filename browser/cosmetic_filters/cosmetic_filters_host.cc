// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/cosmetic_filters/cosmetic_filters_host.h"

#include <algorithm>
#include <utility>

namespace ghost::cosmetic_filters {

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
  // Spike: one fixed generic rule (Task 6 asks the engine).
  std::move(callback).Run(std::ranges::contains(classes, "shade-spike-generic")
                              ? ".shade-spike-generic{display:none !important}\n"
                              : "");
}

}  // namespace ghost::cosmetic_filters
