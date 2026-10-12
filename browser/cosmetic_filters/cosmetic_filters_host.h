// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_HOST_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_HOST_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/document_service.h"
#include "ghost/common/cosmetic_filters.mojom.h"
#include "ghost/components/blocking/blocking_engine.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"

namespace content {
class RenderFrameHost;
}

namespace ghost::cosmetic_filters {

// Answers one document's scanner: the generic rules its class and id names
// match, without the document's exceptions. The renderer isn't trusted: it
// sends only names, within limits; the exceptions and the level stay here.
class CosmeticFiltersHost final : public content::DocumentService<mojom::CosmeticFiltersHost> {
 public:
  // The frame binder (patches/0042).
  static void Create(content::RenderFrameHost* frame,
                     mojo::PendingReceiver<mojom::CosmeticFiltersHost> receiver);

  CosmeticFiltersHost(const CosmeticFiltersHost&) = delete;
  CosmeticFiltersHost& operator=(const CosmeticFiltersHost&) = delete;

  // mojom::CosmeticFiltersHost:
  void GetGenericStyleSheet(const std::vector<std::string>& classes,
                            const std::vector<std::string>& ids,
                            GetGenericStyleSheetCallback callback) override;

 private:
  CosmeticFiltersHost(content::RenderFrameHost& frame,
                      mojo::PendingReceiver<mojom::CosmeticFiltersHost> receiver);
  ~CosmeticFiltersHost() override;

  void OnRules(std::vector<std::string> classes,
               std::vector<std::string> ids,
               GetGenericStyleSheetCallback callback,
               blocking::CosmeticRules rules);
  void Select(std::vector<std::string> classes,
              std::vector<std::string> ids,
              GetGenericStyleSheetCallback callback);

  // The document's exceptions, once the engine gave them.
  std::optional<std::vector<std::string>> exceptions_;
  // A $generichide exception: every answer is empty.
  bool generichide_ = false;
  base::WeakPtrFactory<CosmeticFiltersHost> weak_factory_{this};
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_HOST_H_
