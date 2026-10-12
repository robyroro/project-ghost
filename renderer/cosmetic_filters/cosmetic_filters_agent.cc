// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/renderer/cosmetic_filters/cosmetic_filters_agent.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "ghost/components/cosmetic_filters/names.h"
#include "ghost/renderer/cosmetic_filters/scanner_script.h"
#include "gin/arguments.h"
#include "gin/converter.h"
#include "gin/function_template.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/platform/web_isolated_world_info.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_url.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "url/gurl.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-function.h"
#include "v8/include/v8-object.h"

namespace ghost::cosmetic_filters {
namespace {

constexpr int32_t kWorldId = ISOLATED_WORLD_ID_SHADE_COSMETIC_FILTERS;
// The scanner's name in DevTools and traces; nothing loads it.
constexpr char kScannerUrl[] = "shade://cosmetic-filters/scanner.js";
// The object scanner.js calls, in its world only.
constexpr char kApiName[] = "shadeCosmeticFilters";

void NameTheWorld() {
  [[maybe_unused]] static const bool named = [] {
    blink::WebIsolatedWorldInfo info;
    info.human_readable_name = blink::WebString::FromUtf8("Shade cosmetic filters");
    blink::SetIsolatedWorldInfo(kWorldId, info);
    return true;
  }();
}

}  // namespace

CosmeticFiltersAgent::CosmeticFiltersAgent(content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame),
      content::RenderFrameObserverTracker<CosmeticFiltersAgent>(render_frame) {
  render_frame->GetAssociatedInterfaceRegistry()->AddInterface<mojom::CosmeticFiltersAgent>(
      base::BindRepeating(&CosmeticFiltersAgent::BindReceiver, base::Unretained(this)));
  // A new frame's initial empty document gets no navigation of its own, and a
  // parent's script may write into it.
  // Scripts aren't allowed while a frame is being created: the scanner
  // starts from a task, and its first pass sees what was written meanwhile.
  current_ = InheritedRules();
  if (current_) {
    if (!render_frame->GetWebFrame()->GetDocument().DocumentElement().IsNull()) {
      InsertRules();
    }
    render_frame->GetTaskRunner(blink::TaskType::kInternalDefault)
        ->PostTask(FROM_HERE, base::BindOnce(&CosmeticFiltersAgent::StartScanner,
                                             document_weak_factory_.GetWeakPtr()));
  }
}

CosmeticFiltersAgent::~CosmeticFiltersAgent() = default;

// static
void CosmeticFiltersAgent::RunScriptsAtDocumentStart(content::RenderFrame* render_frame) {
  if (CosmeticFiltersAgent* agent = Get(render_frame)) {
    agent->StartScanner();
  }
}

void CosmeticFiltersAgent::SetRulesForNextDocument(const std::string& style_sheet, bool scan) {
  next_ = Rules{style_sheet, scan};
}

void CosmeticFiltersAgent::DidCreateNewDocument() {
  document_weak_factory_.InvalidateWeakPtrs();
  host_.reset();
  inserted_ = false;
  scanning_ = false;
  current_ = next_ ? std::exchange(next_, std::nullopt) : InheritedRules();
}

void CosmeticFiltersAgent::DidCreateDocumentElement() {
  InsertRules();
}

void CosmeticFiltersAgent::DidCreateScriptContext(v8::Local<v8::Context> context,
                                                  int32_t world_id) {
  if (world_id != kWorldId) {
    return;
  }
  v8::Isolate* isolate = render_frame()->GetWebFrame()->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Object> api = v8::Object::New(isolate);
  api->Set(context, gin::StringToV8(isolate, "report"),
           gin::CreateFunctionTemplate(isolate,
                                       base::BindRepeating(&CosmeticFiltersAgent::Report,
                                                           document_weak_factory_.GetWeakPtr()))
               ->GetFunction(context)
               .ToLocalChecked())
      .Check();
  context->Global()->Set(context, gin::StringToV8(isolate, kApiName), api).Check();
}

void CosmeticFiltersAgent::OnDestruct() {
  delete this;
}

void CosmeticFiltersAgent::BindReceiver(
    mojo::PendingAssociatedReceiver<mojom::CosmeticFiltersAgent> receiver) {
  receiver_.reset();
  receiver_.Bind(std::move(receiver));
}

std::optional<CosmeticFiltersAgent::Rules> CosmeticFiltersAgent::InheritedRules() {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  const GURL url = frame->GetDocument().Url();
  if (!url.IsAboutBlank() && !url.IsAboutSrcdoc()) {
    return std::nullopt;
  }
  blink::WebFrame* parent = frame->Parent();
  if (!parent || !parent->IsWebLocalFrame()) {
    return std::nullopt;
  }
  CosmeticFiltersAgent* parent_agent =
      Get(content::RenderFrame::FromWebFrame(parent->ToWebLocalFrame()));
  return parent_agent ? parent_agent->current_ : std::nullopt;
}

void CosmeticFiltersAgent::InsertRules() {
  if (!current_ || inserted_) {
    return;
  }
  inserted_ = true;
  Insert(current_->style_sheet);
}

void CosmeticFiltersAgent::StartScanner() {
  if (!current_ || !current_->scan || scanning_) {
    return;
  }
  scanning_ = true;
  NameTheWorld();
  render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
      kWorldId,
      blink::WebScriptSource(blink::WebString::FromUtf8(kScannerScript), GURL(kScannerUrl)),
      blink::BackForwardCacheAware::kAllow);
}

void CosmeticFiltersAgent::Report(gin::Arguments* arguments) {
  std::vector<std::string> classes;
  std::vector<std::string> ids;
  if (!arguments->GetNext(&classes) || !arguments->GetNext(&ids)) {
    return;
  }
  // The browser ends a renderer that sends names outside the limits; a page
  // may use odd names, so they're dropped here. The scanner keeps to the count.
  std::erase_if(classes, [](const std::string& name) { return !IsValidName(name); });
  std::erase_if(ids, [](const std::string& name) { return !IsValidName(name); });
  if ((classes.empty() && ids.empty()) || !AreValidNames(classes, ids)) {
    return;
  }
  if (!host_.is_bound()) {
    render_frame()->GetBrowserInterfaceBroker().GetInterface(host_.BindNewPipeAndPassReceiver());
  }
  host_->GetGenericStyleSheet(classes, ids,
                              base::BindOnce(&CosmeticFiltersAgent::Insert,
                                             document_weak_factory_.GetWeakPtr()));
}

void CosmeticFiltersAgent::Insert(const std::string& style_sheet) {
  if (style_sheet.empty()) {
    return;
  }
  render_frame()->GetWebFrame()->GetDocument().InsertStyleSheet(
      blink::WebString::FromUtf8(style_sheet), nullptr, blink::WebCssOrigin::kUser);
}

}  // namespace ghost::cosmetic_filters
