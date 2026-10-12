// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_RENDERER_COSMETIC_FILTERS_COSMETIC_FILTERS_AGENT_H_
#define GHOST_RENDERER_COSMETIC_FILTERS_COSMETIC_FILTERS_AGENT_H_

#include <optional>
#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "ghost/common/cosmetic_filters.mojom.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace gin {
class Arguments;
}

namespace ghost::cosmetic_filters {

// Hides what the lists mark in one frame: inserts the rules the browser sends
// for each document as a user-origin style sheet, and runs the scanner
// (scanner.js) in an isolated world of its own, which reports the document's
// class and id names; the browser answers with the generic rules they match.
// Owns itself; deleted with its frame.
class CosmeticFiltersAgent
    : public content::RenderFrameObserver,
      public content::RenderFrameObserverTracker<CosmeticFiltersAgent>,
      public mojom::CosmeticFiltersAgent {
 public:
  explicit CosmeticFiltersAgent(content::RenderFrame* render_frame);
  CosmeticFiltersAgent(const CosmeticFiltersAgent&) = delete;
  CosmeticFiltersAgent& operator=(const CosmeticFiltersAgent&) = delete;
  ~CosmeticFiltersAgent() override;

  // Starts the frame's scanner for its new document: Blink allows scripts
  // only from here, after the document element exists and before anything
  // is parsed (ChromeContentRendererClient::RunScriptsAtDocumentStart,
  // patches/0041).
  static void RunScriptsAtDocumentStart(content::RenderFrame* render_frame);

  // mojom::CosmeticFiltersAgent:
  void SetRulesForNextDocument(const std::string& style_sheet, bool scan) override;

 private:
  struct Rules {
    std::string style_sheet;
    bool scan = false;
  };

  // content::RenderFrameObserver:
  void DidCreateNewDocument() override;
  void DidCreateDocumentElement() override;
  void DidCreateScriptContext(v8::Local<v8::Context> context, int32_t world_id) override;
  void OnDestruct() override;

  void BindReceiver(mojo::PendingAssociatedReceiver<mojom::CosmeticFiltersAgent> receiver);
  // A document no navigation brought takes its local parent's rules.
  std::optional<Rules> InheritedRules();
  // Each once per document: the rules' style sheet, the scanner.
  void InsertRules();
  void StartScanner();
  // From the scanner: report(classes, ids).
  void Report(gin::Arguments* arguments);
  void Insert(const std::string& style_sheet);

  std::optional<Rules> next_;
  std::optional<Rules> current_;
  bool inserted_ = false;
  bool scanning_ = false;
  mojo::AssociatedReceiver<mojom::CosmeticFiltersAgent> receiver_{this};
  mojo::Remote<mojom::CosmeticFiltersHost> host_;
  // Invalidated at each new document, so no answer reaches the wrong one.
  base::WeakPtrFactory<CosmeticFiltersAgent> document_weak_factory_{this};
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_RENDERER_COSMETIC_FILTERS_COSMETIC_FILTERS_AGENT_H_
