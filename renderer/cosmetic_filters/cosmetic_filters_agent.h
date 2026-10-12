// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_RENDERER_COSMETIC_FILTERS_COSMETIC_FILTERS_AGENT_H_
#define GHOST_RENDERER_COSMETIC_FILTERS_COSMETIC_FILTERS_AGENT_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "ghost/common/cosmetic_filters.mojom.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_set.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_element.h"


namespace gin {
class Arguments;
}

namespace ghost::cosmetic_filters {

// Applies the lists' cosmetic rules in one frame: inserts the rules the
// browser sends for each document as a user-origin style sheet, runs the
// site's scriptlets in the page's own world before its scripts, and runs, in
// an isolated world of its own, the scanner (scanner.js), which reports the
// parts of the document that changed (the agent reads their class and id
// names and the browser answers with the generic rules they match), and the
// procedural rules' executor (procedural.js). Owns itself; deleted with its
// frame.
class CosmeticFiltersAgent
    : public content::RenderFrameObserver,
      public content::RenderFrameObserverTracker<CosmeticFiltersAgent>,
      public mojom::CosmeticFiltersAgent {
 public:
  explicit CosmeticFiltersAgent(content::RenderFrame* render_frame);
  CosmeticFiltersAgent(const CosmeticFiltersAgent&) = delete;
  CosmeticFiltersAgent& operator=(const CosmeticFiltersAgent&) = delete;
  ~CosmeticFiltersAgent() override;

  // Runs the frame's scriptlets and starts its scanner for its new document:
  // Blink allows scripts only from here, after the document element exists
  // and before anything is parsed
  // (ChromeContentRendererClient::RunScriptsAtDocumentStart, patches/0041).
  static void RunScriptsAtDocumentStart(content::RenderFrame* render_frame);

  // mojom::CosmeticFiltersAgent:
  void SetRulesForNextDocument(mojom::DocumentRulesPtr rules) override;

 private:
  struct Rules {
    std::string style_sheet;
    bool scan = false;
    std::string scriptlet;
    std::vector<std::string> procedural_filters;
  };

  // content::RenderFrameObserver:
  void DidCreateNewDocument() override;
  void DidCreateDocumentElement() override;
  void DidCreateScriptContext(v8::Local<v8::Context> context, int32_t world_id) override;
  void OnDestruct() override;

  void BindReceiver(mojo::PendingAssociatedReceiver<mojom::CosmeticFiltersAgent> receiver);
  // A document no navigation brought takes its local parent's rules.
  std::optional<Rules> InheritedRules();
  // Each once per document: the rules' style sheet; the scripts, at its
  // start: the scriptlet, then the scanner.
  void InsertRules();
  void StartScripts();
  void RunScriptlet();
  void StartScanner();
  void StartProcedural();
  // From the scanner: scan(subtrees, elements), the subtrees added and the
  // elements whose class or id changed. Their new names go to the browser in
  // batches within the host's limits.
  void Scan(gin::Arguments* arguments);
  void Collect(const blink::WebElement& element);
  void CollectSubtree(const blink::WebElement& root);
  void SendNames();
  // From procedural.js: filters() returns the document's procedural rules;
  // hide(attribute) and style(attribute, declarations) insert a rule for
  // the elements it marks.
  void Filters(gin::Arguments* arguments);
  void Hide(const std::string& attribute);
  void Style(const std::string& attribute, const std::string& declarations);
  void Insert(const std::string& style_sheet);

  std::optional<Rules> next_;
  std::optional<Rules> current_;
  bool inserted_ = false;
  bool scriptlet_ran_ = false;
  bool scanning_ = false;
  bool procedural_started_ = false;
  // The document's names already sent, and those waiting to be.
  absl::flat_hash_set<std::string> seen_classes_;
  absl::flat_hash_set<std::string> seen_ids_;
  std::vector<std::string> classes_;
  std::vector<std::string> ids_;
  // Made once: Collect() asks every element for it.
  const blink::WebString class_attribute_ = blink::WebString::FromUtf8("class");
  const blink::WebString named_elements_ = blink::WebString::FromUtf8("[id],[class]");
  mojo::AssociatedReceiver<mojom::CosmeticFiltersAgent> receiver_{this};
  mojo::Remote<mojom::CosmeticFiltersHost> host_;
  // Invalidated at each new document, so no answer reaches the wrong one.
  base::WeakPtrFactory<CosmeticFiltersAgent> document_weak_factory_{this};
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_RENDERER_COSMETIC_FILTERS_COSMETIC_FILTERS_AGENT_H_
