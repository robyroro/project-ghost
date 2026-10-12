// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/renderer/cosmetic_filters/cosmetic_filters_agent.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "ghost/components/cosmetic_filters/names.h"
#include "ghost/components/cosmetic_filters/procedural.h"
#include "ghost/components/cosmetic_filters/style_sheet.h"
#include "ghost/renderer/cosmetic_filters/procedural_script.h"
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
constexpr char kProceduralUrl[] = "shade://cosmetic-filters/procedural.js";
// The scriptlets' name in DevTools and stack traces; abort-on-stack-trace
// leaves out frames under shade://.
constexpr char kScriptletsUrl[] = "shade://scriptlets/";
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
        ->PostTask(FROM_HERE, base::BindOnce(&CosmeticFiltersAgent::StartScripts,
                                             document_weak_factory_.GetWeakPtr()));
  }
}

CosmeticFiltersAgent::~CosmeticFiltersAgent() = default;

// static
void CosmeticFiltersAgent::RunScriptsAtDocumentStart(content::RenderFrame* render_frame) {
  if (CosmeticFiltersAgent* agent = Get(render_frame)) {
    agent->StartScripts();
  }
}

void CosmeticFiltersAgent::SetRulesForNextDocument(mojom::DocumentRulesPtr rules) {
  next_ = Rules{std::move(rules->style_sheet), rules->scan, std::move(rules->scriptlet),
                std::move(rules->procedural_filters)};
}

void CosmeticFiltersAgent::DidCreateNewDocument() {
  document_weak_factory_.InvalidateWeakPtrs();
  host_.reset();
  inserted_ = false;
  scriptlet_ran_ = false;
  scanning_ = false;
  procedural_started_ = false;
  seen_classes_.clear();
  seen_ids_.clear();
  classes_.clear();
  ids_.clear();
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
  const auto add = [&](const char* name, v8::Local<v8::FunctionTemplate> function) {
    api->Set(context, gin::StringToV8(isolate, name),
             function->GetFunction(context).ToLocalChecked())
        .Check();
  };
  const base::WeakPtr<CosmeticFiltersAgent> agent = document_weak_factory_.GetWeakPtr();
  add("scan", gin::CreateFunctionTemplate(
                  isolate, base::BindRepeating(&CosmeticFiltersAgent::Scan, agent)));
  add("filters", gin::CreateFunctionTemplate(
                     isolate, base::BindRepeating(&CosmeticFiltersAgent::Filters, agent)));
  add("hide", gin::CreateFunctionTemplate(
                  isolate, base::BindRepeating(&CosmeticFiltersAgent::Hide, agent)));
  add("style", gin::CreateFunctionTemplate(
                   isolate, base::BindRepeating(&CosmeticFiltersAgent::Style, agent)));
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

void CosmeticFiltersAgent::StartScripts() {
  // The scriptlets first: they must run before anything else in the page.
  RunScriptlet();
  StartScanner();
  StartProcedural();
}

void CosmeticFiltersAgent::RunScriptlet() {
  if (!current_ || current_->scriptlet.empty() || scriptlet_ran_) {
    return;
  }
  scriptlet_ran_ = true;
  // One function around them all, so their helpers leave no name on window.
  render_frame()->GetWebFrame()->ExecuteScript(blink::WebScriptSource(
      blink::WebString::FromUtf8(
          base::StrCat({"(function () {\n", current_->scriptlet, "\n})();"})),
      GURL(kScriptletsUrl)));
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

void CosmeticFiltersAgent::StartProcedural() {
  if (!current_ || current_->procedural_filters.empty() || procedural_started_) {
    return;
  }
  procedural_started_ = true;
  NameTheWorld();
  render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
      kWorldId,
      blink::WebScriptSource(blink::WebString::FromUtf8(kProceduralScript), GURL(kProceduralUrl)),
      blink::BackForwardCacheAware::kAllow);
}

void CosmeticFiltersAgent::Filters(gin::Arguments* arguments) {
  arguments->Return(current_ ? current_->procedural_filters : std::vector<std::string>());
}

// procedural.js's markers, checked again here: the isolated world is ours,
// but what it inserts is a user-origin style sheet.
void CosmeticFiltersAgent::Hide(const std::string& attribute) {
  if (IsMarkerAttribute(attribute)) {
    Insert(base::StrCat({"[", attribute, "]{display:none !important}\n"}));
  }
}

void CosmeticFiltersAgent::Style(const std::string& attribute, const std::string& declarations) {
  if (IsMarkerAttribute(attribute) && IsSafeStyle(declarations)) {
    Insert(base::StrCat({"[", attribute, "]{", declarations, "}\n"}));
  }
}

void CosmeticFiltersAgent::Scan(gin::Arguments* arguments) {
  v8::LocalVector<v8::Value> subtrees(arguments->isolate());
  v8::LocalVector<v8::Value> elements(arguments->isolate());
  if (!arguments->GetNext(&subtrees) || !arguments->GetNext(&elements)) {
    return;
  }
  for (v8::Local<v8::Value> value : subtrees) {
    blink::WebElement root = blink::WebElement::FromV8Value(arguments->isolate(), value);
    if (!root.IsNull()) {
      CollectSubtree(root);
    }
  }
  for (v8::Local<v8::Value> value : elements) {
    blink::WebElement element = blink::WebElement::FromV8Value(arguments->isolate(), value);
    if (!element.IsNull()) {
      Collect(element);
    }
  }
  SendNames();
}

void CosmeticFiltersAgent::Collect(const blink::WebElement& element) {
  // The browser ends a renderer that sends names outside the limits; a page
  // may use odd names, so they're dropped here.
  std::string id = element.GetIdAttribute().Utf8();
  if (!id.empty() && !seen_ids_.contains(id) && IsValidName(id)) {
    seen_ids_.insert(id);
    ids_.push_back(std::move(id));
  }
  const std::string names = element.GetAttribute(class_attribute_).Utf8();
  // Split on HTML's whitespace without building a vector per element.
  std::string_view rest(names);
  while (!rest.empty()) {
    const size_t start = rest.find_first_not_of(base::kWhitespaceASCII);
    if (start == std::string_view::npos) {
      break;
    }
    rest.remove_prefix(start);
    const size_t end = std::min(rest.find_first_of(base::kWhitespaceASCII), rest.size());
    const std::string_view name = rest.substr(0, end);
    rest.remove_prefix(end);
    if (!seen_classes_.contains(name) && IsValidName(name)) {
      seen_classes_.emplace(name);
      classes_.emplace_back(name);
    }
  }
}

// The element and its descendants that have a class or an id. One query
// rather than a walk: each WebNode holds a persistent handle, and a walk
// makes several per node, text nodes included.
void CosmeticFiltersAgent::CollectSubtree(const blink::WebElement& root) {
  Collect(root);
  for (const blink::WebElement& element : root.QuerySelectorAll(named_elements_)) {
    Collect(element);
  }
}

void CosmeticFiltersAgent::SendNames() {
  size_t next_class = 0;
  size_t next_id = 0;
  while (next_class < classes_.size() || next_id < ids_.size()) {
    std::vector<std::string> classes;
    std::vector<std::string> ids;
    while (next_class < classes_.size() && classes.size() < kMaxNamesPerCall) {
      classes.push_back(std::move(classes_[next_class++]));
    }
    while (next_id < ids_.size() && classes.size() + ids.size() < kMaxNamesPerCall) {
      ids.push_back(std::move(ids_[next_id++]));
    }
    if (!host_.is_bound()) {
      render_frame()->GetBrowserInterfaceBroker().GetInterface(
          host_.BindNewPipeAndPassReceiver());
    }
    host_->GetGenericStyleSheet(classes, ids,
                                base::BindOnce(&CosmeticFiltersAgent::Insert,
                                               document_weak_factory_.GetWeakPtr()));
  }
  classes_.clear();
  ids_.clear();
}

void CosmeticFiltersAgent::Insert(const std::string& style_sheet) {
  if (style_sheet.empty()) {
    return;
  }
  render_frame()->GetWebFrame()->GetDocument().InsertStyleSheet(
      blink::WebString::FromUtf8(style_sheet), nullptr, blink::WebCssOrigin::kUser);
}

}  // namespace ghost::cosmetic_filters
