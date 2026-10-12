# Cosmetic Filtering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Elements EasyList marks as ads are hidden in every frame: site-specific rules before the page is parsed, generic rules as their classes and ids appear; the page can't see or override it ([spec](../specs/2026-10-11-cosmetic-filtering-design.md)).

**Architecture:**
- **Browser, site-specific rules.** A navigation throttle asks the blocking engine for a document's site-specific rules, deferring the navigation until it answers. A tab helper pushes them to the frame's renderer agent at `ReadyToCommitNavigation` over a channel-associated interface.
- **Renderer.** The agent inserts the rules as a user-origin style sheet and runs a small scanner in an isolated world. The scanner reports class and id names through a gin function.
- **Browser, generic rules.** A per-document `DocumentService` turns the names into the matching generic rules, keeping the exceptions and the level in the browser.
- **Patches.** 0041 (renderer) and 0042 (browser).

**Tech Stack:**
- adblock-rust 0.13.3 through `cxx`;
- Chromium 152.0.7977.158: `NavigationThrottle`, `DocumentService`, `RenderFrameObserver`, Blink's `WebDocument::InsertStyleSheet` and `ExecuteScriptInIsolatedWorld`, gin;
- mojo;
- `ChromeRenderViewHostTestHarness`, `InProcessBrowserTest`, libFuzzer.

**Settled while planning** (each checked in the source at the pin; the spec is amended to match):
- **The rules can't be computed at `ReadyToCommitNavigation`.** The engine answers on its own sequence, and Chromium's subresource filter, the model for the push, sends synchronously there. So `CosmeticFiltersThrottle` defers `WillProcessResponse` and `WillCommitWithoutUrlLoader` until the engine answers. It keeps the result on the navigation (`PreparedRules`, a `NavigationHandleUserData`). `CosmeticFiltersTabHelper`, created by the throttle (no tab-helpers patch), sends it at `ReadyToCommitNavigation`.
- **The browser builds the CSS.** `GetGenericStyleSheet` returns a style sheet, not selectors. `StyleSheetFor`, which drops unsafe selectors, runs only in the trusted process.
- **No `$elemhide`.** adblock-rust 0.13 doesn't support it, and our lists don't use it (0 rules). `$generichide` is used by 171 EasyList rules.
- **Initial empty documents** (a frame without `src`, written by its parent) get no navigation of their own. The agent gives them its local parent's rules.
- **The isolated world** is a new enumerator in `chrome/common/chrome_isolated_world_ids.h`, before the extensions' range; the Java mirror changes with it (upstream's LINT rule).
- **Paths:**
  - mojom: `//ghost/common:mojom`;
  - pure logic: `//ghost/components/cosmetic_filters`;
  - browser: `//ghost/browser/cosmetic_filters` (into `//chrome/browser:core`, as `//ghost/browser/blocking`);
  - renderer: `//ghost/renderer/cosmetic_filters` (into `//chrome/renderer`).

**Settled while executing** ([progress notes](../specs/2026-10-11-cosmetic-filtering-spike.md)):
- **The scanner starts at document start, not in `DidCreateDocumentElement`,** where Blink forbids scripts. Patch 0041 also calls `CosmeticFiltersAgent::RunScriptsAtDocumentStart` from `ChromeContentRendererClient::RunScriptsAtDocumentStart`. An initial empty document's scanner starts from a posted task. The agent's `Apply()` became `InsertRules()` (at `DidCreateDocumentElement`) and `StartScanner()`.
- **The scanner reads `getAttribute('id')` and `getAttribute('class')`,** not `id` and `classList`; it skips `querySelectorAll` on elements without children.
- **Pin APIs:** `WebString::FromUtf8`; `GURL::path()` is a `string_view` (Task 8's `Serve` builds `std::string path(...)`).
- **Task 10 measures with DevTools metrics,** not startup traces: the official build's trace lacked the page's renderer.

**Conventions** (as in the earlier sub-projects):
- **Edit in webops, build in `chromium/src`.** `python <scratchpad>\sync.py` copies changed files into `src/ghost` with new mtimes. Commit in webops; then `git -C chromium/src/ghost pull` (after `git checkout -- .`), and compare `src/ghost` with webops.
- **Patches are commits on `ghost/152.0.7977.158` in `chromium/src`,** with `git commit -s` and `Why:` / `Upstream:` trailers. Re-export with `python tools\patches.py export`; `python tools\patches.py check` must pass.
- **Build and test commands:**
  - Build in `chromium\src` with depot_tools first on PATH: `autoninja -C out\vanilla <targets>`.
  - Unit tests: `out\vanilla\ghost_unittests.exe --gtest_filter=...`.
  - Browser tests run **only on the hidden desktop**: `powershell -File <scratchpad>\hidden.ps1 -CommandLine 'cmd.exe /c "out\vanilla\ghost_browsertests.exe --gtest_filter=CosmeticFilters* > <scratchpad>\bt.log 2>&1"'`, then read the log.
- **Every new file starts with the MPL header** (`tools/lint.py` checks it). The code below leaves it out for brevity; `.js`/`.cc`/`.h`/`.mojom` use `//` comments, `.gn`/`.py` use `#`, `.html` a plain-indented `<!-- -->`.
- **Never chain `git commit` after a test run** in one command. Stage before `python tools\lint.py`, since lint sees only tracked files.
- **Commit messages carry no AI trailer;** the user is the only author.

---

### Task 0: Tools

- [ ] **Step 1:** Copy `hidden.ps1` and `sync.py` from `C:\Users\robyv\AppData\Local\Temp\claude\C--Users-robyv-Desktop-DLU-webops\4f2cca3f-b200-470d-a07f-7104a80791c1\scratchpad\` into this session's scratchpad. If they're gone, recreate them:
  - `hidden.ps1`: `CreateDesktop("shade-tests")` plus `CreateProcess` with `lpDesktop="WinSta0\shade-tests"`; waits for the process and returns its exit code.
  - `sync.py`: copies every webops file (tracked or new) that differs into `chromium/src/ghost` with `shutil.copyfile`; reports files only `src/ghost` has.
- [ ] **Step 2:** Check the state. Run `git -C chromium/src branch --show-current`; expected `ghost/152.0.7977.158`. Run `python tools\patches.py check`; expected to pass. Then `autoninja -C out\vanilla ghost_unittests ghost_browsertests`; expected: no work, or only a small amount.

---

### Task 1: Spike — rules pushed before parsing, a scanner in its world (patches 0041, 0042)

Prove the risky parts with the real structure and fixed rules: a site-specific sheet hiding `.shade-spike`, and a generic answer for the class `shade-spike-generic`. Later tasks replace the fixed rules with the engine.

**Files:**
- Create: `common/BUILD.gn`, `common/cosmetic_filters.mojom`
- Create: `browser/cosmetic_filters/BUILD.gn`, `cosmetic_filters_throttle.{h,cc}`, `prepared_rules.{h,cc}`, `cosmetic_filters_tab_helper.{h,cc}`, `cosmetic_filters_host.{h,cc}`
- Create: `renderer/cosmetic_filters/BUILD.gn`, `cosmetic_filters_agent.{h,cc}`, `scanner.js`
- Create: `browser/cosmetic_filters/cosmetic_filters_browsertest.cc` (spike tests; Task 8 replaces them)
- Modify: `BUILD.gn` (browser test source and deps)
- Create: patches 0041 (renderer) and 0042 (browser) in `chromium/src`
- Create: `docs/superpowers/specs/2026-10-11-cosmetic-filtering-spike.md`

- [ ] **Step 1: The mojom.** `common/cosmetic_filters.mojom`:

```
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

module ghost.mojom;

// Browser to renderer, per frame. Channel-associated and sent at
// ReadyToCommitNavigation, so it arrives before the document it is for, as
// subresource_filter's ActivateForNextCommittedLoad.
interface CosmeticFiltersAgent {
  // The hiding rules of the document the frame commits next: a user-origin
  // style sheet (possibly empty), and whether to scan the document for the
  // generic rules.
  SetRulesForNextDocument(string style_sheet, bool scan);
};

// Renderer to browser, per document (a DocumentService).
interface CosmeticFiltersHost {
  // The generic rules for class and id names the document has, as a
  // user-origin style sheet, possibly empty. At most 1,000 names in a call,
  // each 1 to 256 bytes of UTF-8 (//ghost/components/cosmetic_filters/
  // names.h); a call outside the limits is a bad message.
  GetGenericStyleSheet(array<string> classes, array<string> ids)
      => (string style_sheet);
};
```

`common/BUILD.gn` (license header as in every BUILD.gn):

```gn
import("//mojo/public/tools/bindings/mojom.gni")

# Interfaces between Shade's browser and renderer code.
mojom("mojom") {
  sources = [ "cosmetic_filters.mojom" ]
}
```

- [ ] **Step 2: The renderer agent.** `renderer/cosmetic_filters/cosmetic_filters_agent.h`:

```cpp
#ifndef GHOST_RENDERER_COSMETIC_FILTERS_COSMETIC_FILTERS_AGENT_H_
#define GHOST_RENDERER_COSMETIC_FILTERS_COSMETIC_FILTERS_AGENT_H_

#include <optional>
#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "ghost/common/cosmetic_filters.mojom.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
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
  // Inserts the document's rules and starts the scanner, once per document.
  void Apply();
  // From the scanner: report(classes, ids).
  void Report(gin::Arguments* arguments);
  void Insert(const std::string& style_sheet);

  std::optional<Rules> next_;
  std::optional<Rules> current_;
  bool applied_ = false;
  mojo::AssociatedReceiver<mojom::CosmeticFiltersAgent> receiver_{this};
  mojo::Remote<mojom::CosmeticFiltersHost> host_;
  // Invalidated at each new document, so no answer reaches the wrong one.
  base::WeakPtrFactory<CosmeticFiltersAgent> document_weak_factory_{this};
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_RENDERER_COSMETIC_FILTERS_COSMETIC_FILTERS_AGENT_H_
```

`renderer/cosmetic_filters/cosmetic_filters_agent.cc`:

```cpp
#include "ghost/renderer/cosmetic_filters/cosmetic_filters_agent.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "ghost/renderer/cosmetic_filters/scanner_script.h"
#include "gin/arguments.h"
#include "gin/converter.h"
#include "gin/function_template.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/common/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
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
// The function scanner.js calls, in its world only.
constexpr char kApiName[] = "shadeCosmeticFilters";

void NameTheWorld() {
  [[maybe_unused]] static const bool named = [] {
    blink::WebIsolatedWorldInfo info;
    info.human_readable_name = blink::WebString::FromUTF8("Shade cosmetic filters");
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
  current_ = InheritedRules();
  if (current_ && !render_frame->GetWebFrame()->GetDocument().DocumentElement().IsNull()) {
    Apply();
  }
}

CosmeticFiltersAgent::~CosmeticFiltersAgent() = default;

void CosmeticFiltersAgent::SetRulesForNextDocument(const std::string& style_sheet, bool scan) {
  next_ = Rules{style_sheet, scan};
}

void CosmeticFiltersAgent::DidCreateNewDocument() {
  document_weak_factory_.InvalidateWeakPtrs();
  host_.reset();
  applied_ = false;
  current_ = next_ ? std::exchange(next_, std::nullopt) : InheritedRules();
}

void CosmeticFiltersAgent::DidCreateDocumentElement() {
  Apply();
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

void CosmeticFiltersAgent::Apply() {
  if (!current_ || applied_) {
    return;
  }
  applied_ = true;
  Insert(current_->style_sheet);
  if (current_->scan) {
    NameTheWorld();
    render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
        kWorldId,
        blink::WebScriptSource(blink::WebString::FromUTF8(kScannerScript), GURL(kScannerUrl)),
        blink::BackForwardCacheAware::kAllow);
  }
}

void CosmeticFiltersAgent::Report(gin::Arguments* arguments) {
  std::vector<std::string> classes;
  std::vector<std::string> ids;
  if (!arguments->GetNext(&classes) || !arguments->GetNext(&ids)) {
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
      blink::WebString::FromUTF8(style_sheet), nullptr, blink::WebCssOrigin::kUser);
}

}  // namespace ghost::cosmetic_filters
```

- [ ] **Step 3: The scanner.** `renderer/cosmetic_filters/scanner.js` (MPL header in `//` comments):

```js
// Shade's cosmetic filtering: reports the class and id names of this
// document's elements to the browser, which answers with the generic hiding
// rules they match. Runs in an isolated world of its own
// (CosmeticFiltersAgent), so the page's scripts can't see or change it.
(() => {
  // //ghost/components/cosmetic_filters/names.h: the browser ends a renderer
  // that sends more.
  const MAX_NAMES = 1000;
  // After the first batch, at most one batch in this time.
  const INTERVAL_MS = 100;

  const report = shadeCosmeticFilters.report;
  const seenClasses = new Set();
  const seenIds = new Set();
  let classes = [];
  let ids = [];
  let timer = 0;
  let sentFirst = false;

  function add(element) {
    const id = element.id;
    if (id && !seenIds.has(id)) {
      seenIds.add(id);
      ids.push(id);
    }
    for (const name of element.classList) {
      if (!seenClasses.has(name)) {
        seenClasses.add(name);
        classes.push(name);
      }
    }
  }

  function visit(node) {
    if (node.nodeType !== Node.ELEMENT_NODE) {
      return;
    }
    add(node);
    for (const element of node.querySelectorAll('[id],[class]')) {
      add(element);
    }
  }

  function flush() {
    timer = 0;
    while (classes.length || ids.length) {
      const someClasses = classes.splice(0, MAX_NAMES);
      const someIds = ids.splice(0, MAX_NAMES - someClasses.length);
      report(someClasses, someIds);
    }
  }

  function schedule() {
    if (!classes.length && !ids.length) {
      return;
    }
    if (!sentFirst) {
      sentFirst = true;
      flush();
    } else if (!timer) {
      timer = setTimeout(flush, INTERVAL_MS);
    }
  }

  new MutationObserver(records => {
    for (const record of records) {
      if (record.type === 'attributes') {
        add(record.target);
      } else {
        record.addedNodes.forEach(visit);
      }
    }
    schedule();
  }).observe(document, {
    childList: true,
    subtree: true,
    attributes: true,
    attributeFilter: ['class', 'id'],
  });

  if (document.documentElement) {
    visit(document.documentElement);
    schedule();
  }
})();
```

`renderer/cosmetic_filters/BUILD.gn`:

```gn
# The renderer half of cosmetic filtering. Linked into //chrome/renderer
# (patches/0041), whose content renderer client creates the agent.

# scanner.js in the binary: no grd, no pak.
action("scanner_script") {
  script = "//ghost/tools/embed_text.py"
  inputs = [ "scanner.js" ]
  outputs = [ "$target_gen_dir/scanner_script.h" ]
  args = [
    "--input",
    rebase_path(inputs[0], root_build_dir),
    "--output",
    rebase_path(outputs[0], root_build_dir),
    "--namespace",
    "ghost::cosmetic_filters",
    "--name",
    "kScannerScript",
  ]
}

source_set("cosmetic_filters") {
  sources = [
    "cosmetic_filters_agent.cc",
    "cosmetic_filters_agent.h",
  ]
  public_deps = [
    "//content/public/renderer",
    "//ghost/common:mojom",
    "//mojo/public/cpp/bindings",
  ]
  deps = [
    ":scanner_script",
    "//base",
    "//chrome/common",
    "//gin",
    "//third_party/blink/public:blink",
    "//url",
    "//v8",
  ]
}
```

If `gn check` names a narrower target for `chrome_isolated_world_ids.h` (it is listed in `//chrome/common:common_lib`'s sources), use the one gn names.

- [ ] **Step 4: The browser side, with fixed rules.**

`browser/cosmetic_filters/prepared_rules.h`:

```cpp
#ifndef GHOST_BROWSER_COSMETIC_FILTERS_PREPARED_RULES_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_PREPARED_RULES_H_

#include <string>

#include "content/public/browser/navigation_handle_user_data.h"

namespace ghost::cosmetic_filters {

// A navigation's document's rules, from the throttle to the tab helper.
class PreparedRules : public content::NavigationHandleUserData<PreparedRules> {
 public:
  ~PreparedRules() override;

  const std::string& style_sheet() const { return style_sheet_; }
  bool scan() const { return scan_; }

 private:
  friend NavigationHandleUserData;
  PreparedRules(content::NavigationHandle& navigation, std::string style_sheet, bool scan);

  const std::string style_sheet_;
  const bool scan_;

  NAVIGATION_HANDLE_USER_DATA_KEY_DECL();
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_PREPARED_RULES_H_
```

`prepared_rules.cc`:

```cpp
#include "ghost/browser/cosmetic_filters/prepared_rules.h"

#include <utility>

namespace ghost::cosmetic_filters {

PreparedRules::PreparedRules(content::NavigationHandle& navigation,
                             std::string style_sheet,
                             bool scan)
    : style_sheet_(std::move(style_sheet)), scan_(scan) {}

PreparedRules::~PreparedRules() = default;

NAVIGATION_HANDLE_USER_DATA_KEY_IMPL(PreparedRules);

}  // namespace ghost::cosmetic_filters
```

`cosmetic_filters_tab_helper.h`:

```cpp
#ifndef GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TAB_HELPER_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TAB_HELPER_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace ghost::cosmetic_filters {

// Sends each navigation's prepared rules to its frame's agent just before the
// commit, as subresource_filter sends its activation. Created by the throttle
// with a tab's first navigation.
class CosmeticFiltersTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<CosmeticFiltersTabHelper> {
 public:
  CosmeticFiltersTabHelper(const CosmeticFiltersTabHelper&) = delete;
  CosmeticFiltersTabHelper& operator=(const CosmeticFiltersTabHelper&) = delete;
  ~CosmeticFiltersTabHelper() override;

 private:
  friend WebContentsUserData;
  explicit CosmeticFiltersTabHelper(content::WebContents* contents);

  // content::WebContentsObserver:
  void ReadyToCommitNavigation(content::NavigationHandle* navigation) override;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TAB_HELPER_H_
```

`cosmetic_filters_tab_helper.cc`:

```cpp
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
  agent->SetRulesForNextDocument(rules->style_sheet(), rules->scan());
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(CosmeticFiltersTabHelper);

}  // namespace ghost::cosmetic_filters
```

`cosmetic_filters_throttle.h`:

```cpp
#ifndef GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_THROTTLE_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_THROTTLE_H_

#include "base/memory/weak_ptr.h"
#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationThrottleRegistry;
}

namespace ghost::blocking {
struct CosmeticRules;
}

namespace ghost::cosmetic_filters {

// Prepares the hiding rules of the document a navigation commits: the page's
// level decides whether there are any, and the blocking engine, on its own
// sequence, which; the navigation waits for its answer before the response
// is processed, so the rules reach the renderer before the document is
// parsed (CosmeticFiltersTabHelper sends them).
class CosmeticFiltersThrottle : public content::NavigationThrottle {
 public:
  // Called for every navigation (patches/0042).
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  explicit CosmeticFiltersThrottle(content::NavigationThrottleRegistry& registry);
  CosmeticFiltersThrottle(const CosmeticFiltersThrottle&) = delete;
  CosmeticFiltersThrottle& operator=(const CosmeticFiltersThrottle&) = delete;
  ~CosmeticFiltersThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillProcessResponse() override;
  ThrottleCheckResult WillCommitWithoutUrlLoader() override;
  const char* GetNameForLogging() override;

 private:
  ThrottleCheckResult Prepare();
  void OnRules(blocking::CosmeticRules rules);

  base::WeakPtrFactory<CosmeticFiltersThrottle> weak_factory_{this};
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_THROTTLE_H_
```

The spike's `cosmetic_filters_throttle.cc` prepares fixed rules and never defers. `OnRules` stays declared and undefined until Task 5; nothing calls it, so it links.

```cpp
#include "ghost/browser/cosmetic_filters/cosmetic_filters_throttle.h"

#include <memory>

#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_tab_helper.h"
#include "ghost/browser/cosmetic_filters/prepared_rules.h"

namespace ghost::cosmetic_filters {

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
  // Spike: fixed rules (Task 5 asks the engine).
  PreparedRules::CreateForNavigationHandle(*navigation_handle(),
                                           ".shade-spike{display:none !important}\n", true);
  return PROCEED;
}

}  // namespace ghost::cosmetic_filters
```

`cosmetic_filters_host.h`:

```cpp
#ifndef GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_HOST_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_HOST_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/document_service.h"
#include "ghost/common/cosmetic_filters.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"

namespace content {
class RenderFrameHost;
}

namespace ghost::blocking {
struct CosmeticRules;
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
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_HOST_H_
```

The spike's `cosmetic_filters_host.cc`:

```cpp
#include "ghost/browser/cosmetic_filters/cosmetic_filters_host.h"

#include <algorithm>
#include <utility>

namespace ghost::cosmetic_filters {

// static
void CosmeticFiltersHost::Create(content::RenderFrameHost* frame,
                                 mojo::PendingReceiver<mojom::CosmeticFiltersHost> receiver) {
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
```

(`base/containers/contains.h` is gone at 152: `std::ranges::contains`.)

`browser/cosmetic_filters/BUILD.gn`:

```gn
# The browser half of cosmetic filtering. Linked into //chrome/browser:core
# (patches/0042), whose binders and content browser client use it; so it must
# not depend on //chrome/browser.
source_set("cosmetic_filters") {
  sources = [
    "cosmetic_filters_host.cc",
    "cosmetic_filters_host.h",
    "cosmetic_filters_tab_helper.cc",
    "cosmetic_filters_tab_helper.h",
    "cosmetic_filters_throttle.cc",
    "cosmetic_filters_throttle.h",
    "prepared_rules.cc",
    "prepared_rules.h",
  ]
  public_deps = [
    "//content/public/browser",
    "//ghost/common:mojom",
    "//mojo/public/cpp/bindings",
  ]
  deps = [
    "//base",
    "//third_party/blink/public/common",
    "//url",
  ]
}
```

- [ ] **Step 5: Patch 0041 (renderer).** In `chromium/src`:
  - **`chrome/common/chrome_isolated_world_ids.h`:** before `ISOLATED_WORLD_ID_EXTENSIONS`, add:

    ```cpp
      // Isolated world ID for Shade's cosmetic filtering
      // (//ghost/renderer/cosmetic_filters).
      ISOLATED_WORLD_ID_SHADE_COSMETIC_FILTERS,
    ```

  - **`chrome/android/java/src/org/chromium/chrome/browser/common/ChromeIsolatedWorldIds.java`:** mirror the enumerator as that file does for the others (read it first).
  - **`chrome/renderer/BUILD.gn`:** add `"//ghost/renderer/cosmetic_filters"` to `static_library("renderer")`'s `deps`.
  - **`chrome/renderer/chrome_content_renderer_client.cc`:**
    - include `ghost/renderer/cosmetic_filters/cosmetic_filters_agent.h`;
    - in `RenderFrameCreated`, after `new prerender::NoStatePrefetchRenderFrameObserver(render_frame);`, add:

      ```cpp
        // Shade: cosmetic filtering.
        new ghost::cosmetic_filters::CosmeticFiltersAgent(render_frame);
      ```

  Commit `-s` with subject `chrome_content_renderer_client: Shade's cosmetic filters in every frame`, a body saying why (an observer per frame, an isolated world of its own), and the trailers `Why: renderer frames are created in Chromium's content renderer client only` and `Upstream: not upstreamable: product-specific`.
- [ ] **Step 6: Patch 0042 (browser).**
  - **`chrome/browser/BUILD.gn`:** add `"//ghost/browser/cosmetic_filters"` to `source_set("core")`'s deps, next to `//ghost/browser/blocking`.
  - **`chrome/browser/chrome_browser_interface_binders.cc`:**
    - include `ghost/browser/cosmetic_filters/cosmetic_filters_host.h` and `ghost/common/cosmetic_filters.mojom.h`;
    - in `PopulateChromeFrameBinders`, after the first `map->Add`, add:

      ```cpp
        // Shade: cosmetic filtering's per-document host.
        map->Add<ghost::mojom::CosmeticFiltersHost>(
            &ghost::cosmetic_filters::CosmeticFiltersHost::Create);
      ```

  - **`chrome/browser/chrome_content_browser_client.cc`:**
    - include `ghost/browser/cosmetic_filters/cosmetic_filters_throttle.h`;
    - in `CreateThrottlesForNavigation`, after `CreateAndAddChromeThrottlesForNavigation(registry);`, add:

      ```cpp
        // Shade: cosmetic filtering's rules, last, so a navigation another
        // throttle cancels costs no engine query.
        ghost::cosmetic_filters::CosmeticFiltersThrottle::MaybeCreateAndAdd(registry);
      ```

  Commit `-s` with subject `chrome_content_browser_client: Shade's cosmetic filters' throttle and host` and the trailers `Why: navigation throttles and frame binders are registered in Chromium's lists only` and `Upstream: not upstreamable: product-specific`.
- [ ] **Step 7: Spike tests.** Create `browser/cosmetic_filters/cosmetic_filters_browsertest.cc`. Use the fixture of Task 8 (server, `Tab()`, `Display()`; copy it from there) with these tests:
  - `SpikeSiteSheetBeforeParse`: on `a.test`, the page is `<div class="shade-spike" id="x"></div><script>window.seen = getComputedStyle(document.getElementById('x')).display;</script>`. Expected: `EvalJs("window.seen") == "none"`. Repeat for 30 navigations alternating `a.test` and `b.test`, to force process swaps.
  - `SpikeGeneric`: append `<div class="shade-spike-generic" id="g">`. Expected: `WaitForDisplay("g", "none")`.
  - `SpikeWrittenFrame`: the parent writes `<div class="shade-spike" id="i">` into an iframe without `src`. Expected: hidden.
  - `SpikeSrcdocFrame`: the same through `srcdoc`.
  - `SpikeCsp`: a page served with `Content-Security-Policy: script-src 'none'` and a `shade-spike-generic` element in its markup. Expected: hidden.
  - `SpikePageCantSeeUs`: `typeof shadeCosmeticFilters` is `"undefined"` and `document.styleSheets.length` is `0`.

  Add the file to `ghost_browsertests`' sources in `BUILD.gn`, and `"//ghost/browser/cosmetic_filters"` to its deps.
- [ ] **Step 8: Build and run.** Sync. Run `autoninja -C out\vanilla chrome ghost_browsertests`, then run `CosmeticFiltersBrowserTest.Spike*` on the hidden desktop. Record which pass. A failure tells what the spike found:
  - `SpikeSiteSheetBeforeParse` failing means the ordering isn't guaranteed. Switch to pull: the agent asks the host for the document's sheet at `DidCreateNewDocument` (add `GetRules() => (string style_sheet, bool scan)` to `CosmeticFiltersHost`), and measure how often the inline script sees `block`. **Stop and report to the user** before redesigning.
  - `SpikeWrittenFrame` failing: find which observer methods fire for the initial empty document. Add a temporary `LOG(INFO)` in each override, read the log, and move `Apply()` to the hook that fires; record it.
  - `SpikeCsp` failing: give the world a CSP through `WebIsolatedWorldInfo` (with `security_origin` set to the document's origin) and record why.
- [ ] **Step 9: Tracing.** On `test/cosmetic/heavy.html` (Task 10, Step 1; create it now), trace the dev build:

  ```
  out\vanilla\chrome.exe --user-data-dir=<scratchpad>\trace-profile --no-first-run --trace-startup=devtools.timeline,v8 --trace-startup-file=<scratchpad>\trace.json --trace-startup-duration=8 http://127.0.0.1:<port>/heavy.html
  ```

  Serve `test/cosmetic` with `python -m http.server`, and launch through `hidden.ps1`. In `trace.json`, find the events attributed to `shade://cosmetic-filters/scanner.js`: their names (`FunctionCall`? `EvaluateScript`? `v8.callFunction`?) and the args that carry the URL. Record them; Task 10's script sums them.
- [ ] **Step 10: The rejected option, for the record.**
  - Write the CSS of every EasyList `##` rule that starts with `.` or `#` (one rule per selector) into `<scratchpad>\all-generic.css`, using a few lines of Python in the scratchpad.
  - Make a copy of `heavy.html` in the scratchpad with that sheet inlined in a `<style>`.
  - Trace it as in Step 9 in `out\vanilla`. Record `ParseAuthorStyleSheet` and `UpdateLayoutTree`/`RecalcStyle` totals against the original page, and the renderer's private memory from Task Manager or `--enable-logging`.

  Scratch only: nothing is committed.
- [ ] **Step 11: Notes and commit.**
  - Write the progress notes `docs/superpowers/specs/2026-10-11-cosmetic-filtering-spike.md`: the results of Steps 8–10, build times, surprises.
  - Commit in webops: `cosmetic_filters: rules before parsing and a scanner in its world (patches 0041, 0042): spike`.
  - Re-export the patches; `patches.py check`.

---

### Task 2: The bridge — cosmetic queries in Rust

**Files:**
- Modify: `components/blocking/rust/lib.rs`
- Modify: `components/blocking/engine_bridge_unittest.cc`
- Modify: `components/blocking/BUILD.gn` (`unit_tests` gains `//testing/gmock`)

- [ ] **Step 1: Write the failing tests.** Append to `engine_bridge_unittest.cc` (add `#include <string>`, `#include <vector>`, `#include "testing/gmock/include/gmock/gmock.h"`, and `using testing::ElementsAre; using testing::IsEmpty;` inside the namespace):

```cpp
std::vector<std::string> Strings(const rust::Vec<rust::String>& strings) {
  std::vector<std::string> result;
  for (const rust::String& s : strings) {
    result.emplace_back(s);
  }
  return result;
}

rust::Vec<rust::String> RustStrings(std::initializer_list<const char*> strings) {
  rust::Vec<rust::String> result;
  for (const char* s : strings) {
    result.push_back(rust::String(s));
  }
  return result;
}

TEST(EngineBridgeTest, SiteRulesApplyToTheirSite) {
  rust::Box<Engine> engine = new_engine("a.test##.site-ad\n");
  EXPECT_THAT(Strings(engine->cosmetic_resources("https://www.a.test/page").hide_selectors),
              ElementsAre(".site-ad"));
  EXPECT_THAT(Strings(engine->cosmetic_resources("https://b.test/").hide_selectors), IsEmpty());
}

TEST(EngineBridgeTest, ComplexGenericRulesComeWithEveryPage) {
  rust::Box<Engine> engine = new_engine("##div[class*=\"Ads\"]\n");
  EXPECT_THAT(Strings(engine->cosmetic_resources("https://b.test/").hide_selectors),
              ElementsAre("div[class*=\"Ads\"]"));
}

TEST(EngineBridgeTest, GenericRulesMatchClassesAndIds) {
  rust::Box<Engine> engine = new_engine("##.generic-ad\n###banner\n");
  EXPECT_THAT(Strings(engine->generic_selectors(RustStrings({"generic-ad", "plain"}),
                                                RustStrings({"banner"}), RustStrings({}))),
              ElementsAre("#banner", ".generic-ad"));
  EXPECT_THAT(Strings(engine->generic_selectors(RustStrings({"plain"}), RustStrings({}),
                                                RustStrings({}))),
              IsEmpty());
}

TEST(EngineBridgeTest, AnExceptionRemovesAGenericRuleOnItsSite) {
  rust::Box<Engine> engine = new_engine("##.generic-ad\na.test#@#.generic-ad\n");
  const CosmeticResources a = engine->cosmetic_resources("https://a.test/");
  EXPECT_THAT(Strings(a.exceptions), ElementsAre(".generic-ad"));
  EXPECT_THAT(Strings(engine->generic_selectors(RustStrings({"generic-ad"}), RustStrings({}),
                                                a.exceptions)),
              IsEmpty());
  const CosmeticResources b = engine->cosmetic_resources("https://b.test/");
  EXPECT_THAT(Strings(engine->generic_selectors(RustStrings({"generic-ad"}), RustStrings({}),
                                                b.exceptions)),
              ElementsAre(".generic-ad"));
}

TEST(EngineBridgeTest, GenerichideTurnsGenericRulesOff) {
  rust::Box<Engine> engine =
      new_engine("##div[class*=\"Ads\"]\nnohide.test##.site-ad\n@@||nohide.test^$generichide\n");
  const CosmeticResources resources = engine->cosmetic_resources("https://nohide.test/");
  EXPECT_TRUE(resources.generichide);
  // The site's own rules stay; the generic ones go.
  EXPECT_THAT(Strings(resources.hide_selectors), ElementsAre(".site-ad"));
  EXPECT_FALSE(engine->cosmetic_resources("https://b.test/").generichide);
}

TEST(EngineBridgeTest, AnUnparsableUrlHasNoCosmeticRules) {
  rust::Box<Engine> engine = new_engine("##div[class*=\"Ads\"]\n");
  EXPECT_THAT(Strings(engine->cosmetic_resources("not a url").hide_selectors), IsEmpty());
}
```

- [ ] **Step 2: Run them to see them fail.** Sync. Run `autoninja -C out\vanilla ghost_unittests`. Expected: a compile failure (no `cosmetic_resources`).
- [ ] **Step 3: Implement.** In `lib.rs`:
  - Add `use std::collections::HashSet;`.
  - Inside `mod ffi`, after `Verdict`:

    ```rust
        /// A document's cosmetic rules: what to hide on its site, and what
        /// the generic rules mustn't hide there.
        struct CosmeticResources {
            hide_selectors: Vec<String>,
            exceptions: Vec<String>,
            /// A `$generichide` exception: no generic rule on this page.
            generichide: bool,
        }
    ```

  - Inside `extern "Rust"`, after `check`:

    ```rust
            /// The site-specific selectors (and the generic ones too complex
            /// to index) for a document's URL, its exceptions and generichide.
            /// A URL the engine can't parse has none. Sorted, so answers are
            /// stable.
            fn cosmetic_resources(self: &Engine, url: &str) -> CosmeticResources;
            /// The generic selectors the class and id names match, less the
            /// document's exceptions. Sorted.
            fn generic_selectors(
                self: &Engine,
                classes: &Vec<String>,
                ids: &Vec<String>,
                exceptions: &Vec<String>,
            ) -> Vec<String>;
    ```

  - In `new_engine`, replace the `RuleTypes::NetworkOnly` options with `ParseOptions::default()`, which parses every rule type. Replace the comment above it with: `// Every rule type: network rules for requests, cosmetic rules for pages (Phase 4A); scriptlets and procedural rules are parsed but not used until 4B.`. Drop the now-unused `RuleTypes` import.
  - In `impl Engine`:

    ```rust
        fn cosmetic_resources(&self, url: &str) -> ffi::CosmeticResources {
            let resources = self.0.url_cosmetic_resources(url);
            ffi::CosmeticResources {
                hide_selectors: sorted(resources.hide_selectors),
                exceptions: sorted(resources.exceptions),
                generichide: resources.generichide,
            }
        }

        fn generic_selectors(
            &self,
            classes: &Vec<String>,
            ids: &Vec<String>,
            exceptions: &Vec<String>,
        ) -> Vec<String> {
            let exceptions: HashSet<String> = exceptions.iter().cloned().collect();
            let mut selectors = self.0.hidden_class_id_selectors(classes, ids, &exceptions);
            selectors.sort();
            selectors
        }
    ```

  - At the end of the file:

    ```rust
    fn sorted(set: HashSet<String>) -> Vec<String> {
        let mut strings: Vec<String> = set.into_iter().collect();
        strings.sort();
        strings
    }
    ```

  - Update the module comment: `//! adblock-rust behind a small cxx bridge: build an engine from list text, ask it about one request, or about a document's cosmetic rules. Everything else (threads, lifetimes, policy) is C++'s, in //ghost/components/blocking.`.
- [ ] **Step 4: Run them to see them pass.** Run `autoninja -C out\vanilla ghost_unittests`, then `out\vanilla\ghost_unittests.exe --gtest_filter=EngineBridgeTest.*`. Expected: all pass, the network tests included.
- [ ] **Step 5: Commit** (webops): `blocking: the engine answers cosmetic queries`.

---

### Task 3: `BlockingEngine`'s cosmetic methods

**Files:**
- Modify: `components/blocking/blocking_engine.{h,cc}`, `blocking_engine_unittest.cc`

- [ ] **Step 1: Write the failing tests.** Append to `blocking_engine_unittest.cc` (add gmock as in Task 2):

```cpp
TEST_F(BlockingEngineTest, CosmeticRulesForADocument) {
  BlockingEngine engine;
  engine.Load({"a.test##.site-ad\n##.generic-ad\na.test#@#.generic-ad\n"});
  base::test::TestFuture<CosmeticRules> rules;
  engine.GetCosmeticRules(GURL("https://a.test/"), rules.GetCallback());
  EXPECT_THAT(rules.Get().hide_selectors, testing::ElementsAre(".site-ad"));
  EXPECT_THAT(rules.Get().exceptions, testing::ElementsAre(".generic-ad"));
  EXPECT_FALSE(rules.Get().generichide);
}

TEST_F(BlockingEngineTest, GenericSelectorsLessExceptions) {
  BlockingEngine engine;
  engine.Load({"##.generic-ad\n###banner\n"});
  base::test::TestFuture<std::vector<std::string>> selectors;
  engine.GetGenericSelectors({"generic-ad"}, {"banner"}, {".generic-ad"}, selectors.GetCallback());
  EXPECT_THAT(selectors.Get(), testing::ElementsAre("#banner"));
}

TEST_F(BlockingEngineTest, CosmeticQueriesBeforeLoadingAreAnsweredAfterIt) {
  BlockingEngine engine;
  base::test::TestFuture<CosmeticRules> rules;
  engine.GetCosmeticRules(GURL("https://a.test/"), rules.GetCallback());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(rules.IsReady()) << "answered before the lists loaded";
  engine.Load({"a.test##.site-ad\n"});
  EXPECT_THAT(rules.Get().hide_selectors, testing::ElementsAre(".site-ad"));
}

TEST_F(BlockingEngineTest, AFailedEngineHidesNothing) {
  BlockingEngine engine;
  engine.Load({});
  base::test::TestFuture<CosmeticRules> rules;
  engine.GetCosmeticRules(GURL("https://a.test/"), rules.GetCallback());
  EXPECT_TRUE(rules.Get().hide_selectors.empty());
  base::test::TestFuture<std::vector<std::string>> selectors;
  engine.GetGenericSelectors({"generic-ad"}, {}, {}, selectors.GetCallback());
  EXPECT_TRUE(selectors.Get().empty());
}

// Names come from pages; cxx aborts on a Rust string that isn't UTF-8.
TEST_F(BlockingEngineTest, NamesThatAreNotUtf8AreLeftOut) {
  BlockingEngine engine;
  engine.Load({"##.generic-ad\n"});
  base::test::TestFuture<std::vector<std::string>> selectors;
  engine.GetGenericSelectors({"\xff", "generic-ad"}, {"\xc3\x28"}, {}, selectors.GetCallback());
  EXPECT_THAT(selectors.Get(), testing::ElementsAre(".generic-ad"));
}
```

- [ ] **Step 2: Run them to see them fail.** Expected: a compile failure (no `CosmeticRules`).
- [ ] **Step 3: Implement.** In `blocking_engine.h`, after `Decision`:

```cpp
// A document's cosmetic rules (Phase 4A).
struct CosmeticRules {
  CosmeticRules();
  CosmeticRules(CosmeticRules&&);
  CosmeticRules& operator=(CosmeticRules&&);
  ~CosmeticRules();

  // Selectors to hide on the document's site, and the generic ones too
  // complex to look up by class or id.
  std::vector<std::string> hide_selectors;
  // Generic selectors the document's site excepts.
  std::vector<std::string> exceptions;
  // A $generichide exception: no generic rule applies.
  bool generichide = false;
};
```

In `class BlockingEngine`, after `Check`:

```cpp
  // A document's cosmetic rules; empty for a URL the engine can't parse, and
  // from an engine that failed. Waits while the engine loads, as Check does.
  void GetCosmeticRules(GURL document, base::OnceCallback<void(CosmeticRules)> reply);

  // The generic selectors the class and id names match, less `exceptions`
  // (a document's CosmeticRules::exceptions). Names that aren't UTF-8 are
  // left out.
  void GetGenericSelectors(std::vector<std::string> classes,
                           std::vector<std::string> ids,
                           std::vector<std::string> exceptions,
                           base::OnceCallback<void(std::vector<std::string>)> reply);
```

In `blocking_engine.cc`:
- Change `pending_` to `std::vector<base::OnceClosure> pending_;`.
- In `Load`, replace the replay loop with `for (base::OnceClosure& answer : std::exchange(pending_, {})) { std::move(answer).Run(); }`.
- Add to `Core`:

```cpp
  void Check(CheckRequest request, base::OnceCallback<void(Decision)> reply) {
    WhenLoaded(base::BindOnce(&Core::Answer, base::Unretained(this), std::move(request),
                              std::move(reply)));
  }

  void GetCosmeticRules(GURL document, base::OnceCallback<void(CosmeticRules)> reply) {
    WhenLoaded(base::BindOnce(&Core::AnswerCosmeticRules, base::Unretained(this),
                              std::move(document), std::move(reply)));
  }

  void GetGenericSelectors(std::vector<std::string> classes,
                           std::vector<std::string> ids,
                           std::vector<std::string> exceptions,
                           base::OnceCallback<void(std::vector<std::string>)> reply) {
    WhenLoaded(base::BindOnce(&Core::AnswerGenericSelectors, base::Unretained(this),
                              std::move(classes), std::move(ids), std::move(exceptions),
                              std::move(reply)));
  }

 private:
  // Queries made while the engine loads wait for it, so the first pages after
  // startup don't escape it. Core owns the closures, so Unretained is safe.
  void WhenLoaded(base::OnceClosure answer) {
    if (state_ == State::kLoading) {
      pending_.push_back(std::move(answer));
    } else {
      std::move(answer).Run();
    }
  }

  void AnswerCosmeticRules(const GURL& document,
                           base::OnceCallback<void(CosmeticRules)> reply) {
    CosmeticRules rules;
    // Valid URLs' specs are ASCII.
    if (state_ == State::kReady && document.is_valid()) {
      CosmeticResources resources = (*engine_)->cosmetic_resources(document.spec());
      for (const rust::String& selector : resources.hide_selectors) {
        rules.hide_selectors.emplace_back(selector);
      }
      for (const rust::String& selector : resources.exceptions) {
        rules.exceptions.emplace_back(selector);
      }
      rules.generichide = resources.generichide;
    }
    std::move(reply).Run(std::move(rules));
  }

  void AnswerGenericSelectors(const std::vector<std::string>& classes,
                              const std::vector<std::string>& ids,
                              const std::vector<std::string>& exceptions,
                              base::OnceCallback<void(std::vector<std::string>)> reply) {
    std::vector<std::string> selectors;
    if (state_ == State::kReady) {
      for (const rust::String& selector : (*engine_)->generic_selectors(
               ToRust(classes), ToRust(ids), ToRust(exceptions))) {
        selectors.emplace_back(selector);
      }
    }
    std::move(reply).Run(std::move(selectors));
  }

  // The bridge's strings must be UTF-8: cxx aborts the process otherwise.
  static rust::Vec<rust::String> ToRust(const std::vector<std::string>& strings) {
    rust::Vec<rust::String> result;
    for (const std::string& s : strings) {
      if (base::IsStringUTF8AllowingNoncharacters(s)) {
        result.push_back(rust::String(s));
      }
    }
    return result;
  }
```

- The existing `Check` body becomes the one above (the `kLoading` branch moves into `WhenLoaded`).
- Define `CosmeticRules`' special members `= default` at the top of the namespace in the `.cc`.
- Add the two public forwarding methods after `BlockingEngine::Check`:

```cpp
void BlockingEngine::GetCosmeticRules(GURL document,
                                      base::OnceCallback<void(CosmeticRules)> reply) {
  core_.AsyncCall(&Core::GetCosmeticRules)
      .WithArgs(std::move(document), base::BindPostTaskToCurrentDefault(std::move(reply)));
}

void BlockingEngine::GetGenericSelectors(
    std::vector<std::string> classes,
    std::vector<std::string> ids,
    std::vector<std::string> exceptions,
    base::OnceCallback<void(std::vector<std::string>)> reply) {
  core_.AsyncCall(&Core::GetGenericSelectors)
      .WithArgs(std::move(classes), std::move(ids), std::move(exceptions),
                base::BindPostTaskToCurrentDefault(std::move(reply)));
}
```

- Update the class comment: `adblock-rust's engine, on a sequence of its own: building it from the lists takes about a second, and every query runs off the thread that asks. One engine serves every profile and tab, for requests (3A) and pages' cosmetic rules (4A).`.
- [ ] **Step 4: Run them to see them pass.** Run `out\vanilla\ghost_unittests.exe --gtest_filter=BlockingEngineTest.*:EngineBridgeTest.*`. Expected: all pass.
- [ ] **Step 5: Commit:** `blocking: cosmetic queries on the engine's sequence`.

---

### Task 4: Style sheets and name limits (no `//content`)

**Files:**
- Create: `components/cosmetic_filters/BUILD.gn`, `style_sheet.{h,cc}`, `names.{h,cc}`, `style_sheet_unittest.cc`, `names_unittest.cc`
- Modify: `BUILD.gn` (`ghost_unittests` deps += `//ghost/components/cosmetic_filters:unit_tests`)

- [ ] **Step 1: Write the failing tests.** `style_sheet_unittest.cc`:

```cpp
#include "ghost/components/cosmetic_filters/style_sheet.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::cosmetic_filters {
namespace {

TEST(StyleSheetTest, OneRulePerSelector) {
  EXPECT_EQ(StyleSheetFor({".a", "#b > div"}),
            ".a{display:none !important}\n#b > div{display:none !important}\n");
}

TEST(StyleSheetTest, NoSelectorsNoSheet) {
  EXPECT_EQ(StyleSheetFor({}), "");
}

// Lists will arrive by update (3E): a selector that closes its rule would
// inject arbitrary style.
TEST(StyleSheetTest, UnsafeSelectorsAreDropped) {
  for (const char* unsafe :
       {".a{color:red}", ".a}body{", ".a;", "@import url(x)", ".a/*", ".a*/", ".a\\", ".a\n.b",
        ".a\r", ".a\f", ""}) {
    SCOPED_TRACE(unsafe);
    EXPECT_EQ(StyleSheetFor({unsafe, ".ok"}), ".ok{display:none !important}\n");
  }
}

}  // namespace
}  // namespace ghost::cosmetic_filters
```

`names_unittest.cc`:

```cpp
#include "ghost/components/cosmetic_filters/names.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::cosmetic_filters {
namespace {

TEST(NamesTest, AName) {
  EXPECT_TRUE(IsValidName("ad-banner"));
  EXPECT_TRUE(IsValidName(std::string(kMaxNameLength, 'a')));
  EXPECT_FALSE(IsValidName(std::string(kMaxNameLength + 1, 'a')));
  EXPECT_FALSE(IsValidName(""));
  EXPECT_FALSE(IsValidName("\xff"));
}

TEST(NamesTest, ACall) {
  EXPECT_TRUE(AreValidNames(std::vector<std::string>(kMaxNamesPerCall - 1, "c"), {"i"}));
  EXPECT_FALSE(AreValidNames(std::vector<std::string>(kMaxNamesPerCall, "c"), {"i"}));
  EXPECT_FALSE(AreValidNames({"c"}, {""}));
}

}  // namespace
}  // namespace ghost::cosmetic_filters
```

- [ ] **Step 2: Run them to see them fail.** Add the BUILD.gn below with the sources missing, so the build fails.
- [ ] **Step 3: Implement.** `style_sheet.h`:

```cpp
#ifndef GHOST_COMPONENTS_COSMETIC_FILTERS_STYLE_SHEET_H_
#define GHOST_COMPONENTS_COSMETIC_FILTERS_STYLE_SHEET_H_

#include <string>
#include <vector>

namespace ghost::cosmetic_filters {

// A user-origin style sheet hiding what the selectors match, one rule per
// selector: in a selector list one invalid selector drops the whole rule.
// Selectors that could end their rule or start another ({, }, ;, @,
// comments, a trailing backslash, newlines) are dropped.
std::string StyleSheetFor(const std::vector<std::string>& selectors);

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_COMPONENTS_COSMETIC_FILTERS_STYLE_SHEET_H_
```

`style_sheet.cc`:

```cpp
#include "ghost/components/cosmetic_filters/style_sheet.h"

#include <string_view>

namespace ghost::cosmetic_filters {
namespace {

bool IsSafe(std::string_view selector) {
  if (selector.empty() || selector.back() == '\\') {
    return false;
  }
  for (std::string_view unsafe : {"{", "}", ";", "@", "/*", "*/", "\n", "\r", "\f"}) {
    if (selector.find(unsafe) != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::string StyleSheetFor(const std::vector<std::string>& selectors) {
  std::string sheet;
  for (const std::string& selector : selectors) {
    if (IsSafe(selector)) {
      sheet += selector;
      sheet += "{display:none !important}\n";
    }
  }
  return sheet;
}

}  // namespace ghost::cosmetic_filters
```

`names.h`:

```cpp
#ifndef GHOST_COMPONENTS_COSMETIC_FILTERS_NAMES_H_
#define GHOST_COMPONENTS_COSMETIC_FILTERS_NAMES_H_

#include <stddef.h>

#include <string>
#include <string_view>
#include <vector>

namespace ghost::cosmetic_filters {

// The limits on what a renderer's scanner sends in one call
// (mojom::CosmeticFiltersHost): more is a bad message.
inline constexpr size_t kMaxNamesPerCall = 1000;
inline constexpr size_t kMaxNameLength = 256;  // Bytes of UTF-8.

bool IsValidName(std::string_view name);
bool AreValidNames(const std::vector<std::string>& classes, const std::vector<std::string>& ids);

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_COMPONENTS_COSMETIC_FILTERS_NAMES_H_
```

`names.cc`:

```cpp
#include "ghost/components/cosmetic_filters/names.h"

#include <algorithm>

#include "base/strings/string_util.h"

namespace ghost::cosmetic_filters {

bool IsValidName(std::string_view name) {
  return !name.empty() && name.size() <= kMaxNameLength &&
         base::IsStringUTF8AllowingNoncharacters(name);
}

bool AreValidNames(const std::vector<std::string>& classes, const std::vector<std::string>& ids) {
  return classes.size() + ids.size() <= kMaxNamesPerCall &&
         std::ranges::all_of(classes, IsValidName) && std::ranges::all_of(ids, IsValidName);
}

}  // namespace ghost::cosmetic_filters
```

`BUILD.gn`:

```gn
# Cosmetic filtering's logic that needs no browser
# (docs/superpowers/specs/2026-10-11-cosmetic-filtering-design.md).
source_set("cosmetic_filters") {
  sources = [
    "names.cc",
    "names.h",
    "style_sheet.cc",
    "style_sheet.h",
  ]
  deps = [ "//base" ]
}

source_set("unit_tests") {
  testonly = true
  sources = [
    "names_unittest.cc",
    "style_sheet_unittest.cc",
  ]
  deps = [
    ":cosmetic_filters",
    "//testing/gtest",
  ]
}
```

- [ ] **Step 4: Run them to see them pass.** Filter `StyleSheetTest.*:NamesTest.*`. Expected: pass.
- [ ] **Step 5: Commit:** `cosmetic_filters: style sheets and the limits on names`.

---

### Task 5: The throttle asks the engine, by the page's level

**Files:**
- Modify: `browser/cosmetic_filters/cosmetic_filters_throttle.{h,cc}`, `BUILD.gn`

- [ ] **Step 1: The tests.** The browser tests of Task 8 cover the throttle: site rules, Off, Incognito, frames. Write them now as failing tests: in Task 8's file, write `SiteRuleHidesBeforeThePageSeesIt`, `OffHidesNothing`, `IncognitoHides` and `ACrossSiteFrameHasItsOwnRules`, and delete the spike tests. Run them; expected: they fail, since the fixed spike rules hide `.shade-spike` only.
- [ ] **Step 2: Implement.** In the `.cc` (`OnRules` is already declared):

```cpp
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/components/blocking/blocking_engine.h"
#include "ghost/components/cosmetic_filters/style_sheet.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/origin.h"

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

content::NavigationThrottle::ThrottleCheckResult CosmeticFiltersThrottle::Prepare() {
  content::NavigationHandle& navigation = *navigation_handle();
  const GURL document = DocumentUrl(navigation);
  // Browser pages, files, extensions: no rules.
  if (!document.SchemeIsHTTPOrHTTPS()) {
    return PROCEED;
  }
  // The level is the page's: its outermost main frame (as for requests).
  const GURL page = navigation.IsInOutermostMainFrame()
                        ? document
                        : navigation.GetParentFrameOrOuterDocument()
                              ->GetOutermostMainFrame()
                              ->GetLastCommittedURL();
  if (privacy_policy::GetLevel(navigation.GetWebContents()->GetBrowserContext(), page) ==
      privacy_policy::ProtectionLevel::kOff) {
    PreparedRules::CreateForNavigationHandle(navigation, std::string(), false);
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
  PreparedRules::CreateForNavigationHandle(*navigation_handle(),
                                           StyleSheetFor(rules.hide_selectors),
                                           !rules.generichide);
  Resume();
}
```

Add the deps:
- `public_deps`: `//ghost/components/blocking` (for `CosmeticRules`).
- `deps`: `//ghost/browser/blocking`, `//ghost/browser/privacy_policy`, `//ghost/components/cosmetic_filters`, `//ghost/components/privacy_policy`.

- [ ] **Step 3: Run them to see them pass.** Run `CosmeticFiltersBrowserTest.*` on the hidden desktop. Expected: the four tests pass.
- [ ] **Step 4: Commit:** `cosmetic_filters: a document's rules from the engine, by the page's level`.

---

### Task 6: The host — generic rules for the scanner's names

**Files:**
- Modify: `browser/cosmetic_filters/cosmetic_filters_host.{h,cc}`
- Create: `browser/cosmetic_filters/cosmetic_filters_host_unittest.cc`
- Modify: `BUILD.gn` (`ghost_unittests`: the source; deps `//ghost/browser/cosmetic_filters`, `//ghost/common:mojom`, `//mojo/public/cpp/test_support:test_utils`)

- [ ] **Step 1: Write the failing tests.** `cosmetic_filters_host_unittest.cc`:

```cpp
#include "ghost/browser/cosmetic_filters/cosmetic_filters_host.h"

#include <string>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/test/test_future.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "ghost/components/cosmetic_filters/names.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/test_support/test_utils.h"
#include "url/gurl.h"

namespace ghost::cosmetic_filters {
namespace {

class CosmeticFiltersHostTest : public ChromeRenderViewHostTestHarness {
 protected:
  mojo::Remote<mojom::CosmeticFiltersHost> BindHost() {
    NavigateAndCommit(GURL("https://a.test/"));
    mojo::Remote<mojom::CosmeticFiltersHost> host;
    CosmeticFiltersHost::Create(main_rfh(), host.BindNewPipeAndPassReceiver());
    return host;
  }
};

// The renderer isn't trusted: names beyond the limits end it.
TEST_F(CosmeticFiltersHostTest, TooManyNamesAreABadMessage) {
  mojo::Remote<mojom::CosmeticFiltersHost> host = BindHost();
  mojo::test::BadMessageObserver bad_message;
  host->GetGenericStyleSheet(std::vector<std::string>(kMaxNamesPerCall + 1, "c"), {},
                             base::DoNothing());
  EXPECT_EQ(bad_message.WaitForBadMessage(), "CosmeticFiltersHost: names outside the limits");
}

TEST_F(CosmeticFiltersHostTest, ANameThatIsNotUtf8IsABadMessage) {
  mojo::Remote<mojom::CosmeticFiltersHost> host = BindHost();
  mojo::test::BadMessageObserver bad_message;
  host->GetGenericStyleSheet({"\xff"}, {}, base::DoNothing());
  EXPECT_EQ(bad_message.WaitForBadMessage(), "CosmeticFiltersHost: names outside the limits");
}

// Unit tests start no blocking service.
TEST_F(CosmeticFiltersHostTest, WithoutTheEngineNothingIsHidden) {
  mojo::Remote<mojom::CosmeticFiltersHost> host = BindHost();
  base::test::TestFuture<const std::string&> style_sheet;
  host->GetGenericStyleSheet({"generic-ad"}, {}, style_sheet.GetCallback());
  EXPECT_EQ(style_sheet.Get(), "");
}

}  // namespace
}  // namespace ghost::cosmetic_filters
```

- [ ] **Step 2: Run them to see them fail.** Expected: the two bad-message tests time out or fail; the spike host validates nothing.
- [ ] **Step 3: Implement.** In the header, add to the private section:

```cpp
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
```

`cosmetic_filters_host.cc` replaces the spike body:

```cpp
#include "ghost/browser/cosmetic_filters/cosmetic_filters_host.h"

#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/render_frame_host.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/components/blocking/blocking_engine.h"
#include "ghost/components/cosmetic_filters/names.h"
#include "ghost/components/cosmetic_filters/style_sheet.h"
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
```

In the header, add `#include "ghost/components/blocking/blocking_engine.h"` (for `CosmeticRules` by value), and remove the forward declaration.
- [ ] **Step 4: Run them to see them pass.** Filter `CosmeticFiltersHostTest.*`. Expected: pass.
- [ ] **Step 5: Commit:** `cosmetic_filters: the host answers a document's scanner`.

---

### Task 7: The agent drops names outside the limits

**Files:**
- Modify: `renderer/cosmetic_filters/cosmetic_filters_agent.cc`, `BUILD.gn`

- [ ] **Step 1: The test.** A page with a 300-character class name and a listed one on the same element must still hide it, and the renderer must survive. Add `ALongClassNameDoesntEndTheRenderer` to Task 8's file:
  - the page appends `<div class="${'x'.repeat(300)} generic-ad" id="long">`;
  - wait for `#long` to be `none`;
  - `EXPECT_FALSE(Tab()->IsCrashed())`.

  Run it. Expected: it fails (the browser ends the renderer as a bad message).
- [ ] **Step 2: Implement.** In `Report`, after reading the arguments:

```cpp
  // The browser ends a renderer that sends names outside the limits; a page
  // may use odd names, so they're dropped here. The scanner keeps to the count.
  std::erase_if(classes, [](const std::string& name) { return !IsValidName(name); });
  std::erase_if(ids, [](const std::string& name) { return !IsValidName(name); });
  if ((classes.empty() && ids.empty()) || !AreValidNames(classes, ids)) {
    return;
  }
```

Include `ghost/components/cosmetic_filters/names.h`, and add `"//ghost/components/cosmetic_filters"` to the renderer target's deps.
- [ ] **Step 3: Run it to see it pass.** Expected: pass.
- [ ] **Step 4: Commit:** `cosmetic_filters: the agent keeps to the limits on names`.

---

### Task 8: Browser tests

**Files:**
- Modify: `browser/cosmetic_filters/cosmetic_filters_browsertest.cc`

- [ ] **Step 1: The fixture and every test.** Some of these were written in Tasks 5 and 7; this is the whole file.

```cpp
// Cosmetic filtering through the browser, the renderer and the engine, with a
// test list, on an embedded test server reached through host-resolver rules.

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace ghost::cosmetic_filters {
namespace {

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

constexpr char kTestList[] =
    "a.test##.site-ad\n"
    "c.test##.frame-ad\n"
    "nohide.test##.site-ad\n"
    "##.generic-ad\n"
    "###banner\n"
    "##.allowed-ad\n"
    "a.test#@#.allowed-ad\n"
    "@@||nohide.test^$generichide\n";

// The pages, by path.
const std::map<std::string, std::string>& Pages() {
  static const base::NoDestructor<std::map<std::string, std::string>> pages({
      {"/site.html",
       "<div class=\"site-ad\" id=\"site\">ad</div>"
       "<script>window.seenAtParse ="
       " getComputedStyle(document.getElementById('site')).display;</script>"},
      {"/generic.html",
       "<div class=\"generic-ad\" id=\"generic\">ad</div>"
       "<div class=\"x\" id=\"banner\">ad</div>"
       "<div class=\"allowed-ad\" id=\"allowed\">ad</div>"
       "<div class=\"site-ad\" id=\"site\">ad</div>"
       "<div class=\"plain\" id=\"plain\">text</div>"},
      {"/override.html",
       "<style>.site-ad{display:block !important}</style>"
       "<div class=\"site-ad\" id=\"site\">ad</div>"},
      {"/framed.html",
       "<div class=\"frame-ad\" id=\"frame-ad\">ad</div>"
       "<div class=\"site-ad\" id=\"site\">ad</div>"},
      {"/written.html",
       "<iframe id=\"f\"></iframe><script>"
       "document.getElementById('f').contentDocument.body.innerHTML ="
       " '<div class=\"site-ad\" id=\"inner\">ad</div>';</script>"},
      {"/page.html", "<p>page</p>"},
  });
  return *pages;
}

std::unique_ptr<HttpResponse> Serve(const HttpRequest& request) {
  auto response = std::make_unique<BasicHttpResponse>();
  const GURL url = request.GetURL();
  const std::string path(url.path() == "/csp.html" ? "/generic.html" : url.path());
  auto page = Pages().find(path);
  if (page == Pages().end()) {
    response->set_code(net::HTTP_NOT_FOUND);
    return response;
  }
  if (url.path() == "/csp.html") {
    response->AddCustomHeader("Content-Security-Policy", "script-src 'none'");
  }
  response->set_content_type("text/html");
  response->set_content("<!doctype html><html><body>" + page->second + "</body></html>");
  return response;
}

class CosmeticFiltersBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(blocking::BlockingService::GetIfStarted());
    blocking::BlockingService::GetIfStarted()->SetListsForTesting({kTestList});
  }

  GURL Url(std::string_view host, std::string_view path) {
    return embedded_test_server()->GetURL(host, path);
  }

  content::WebContents* Tab(Browser* browser = nullptr) {
    return (browser ? browser : this->browser())->tab_strip_model()->GetActiveWebContents();
  }

  // The element's computed display, at once.
  std::string Display(const content::ToRenderFrameHost& frame, std::string_view id) {
    return content::EvalJs(frame, content::JsReplace(
                                      "getComputedStyle(document.getElementById($1)).display", id))
        .ExtractString();
  }

  // The element's computed display once it is `expected`, or after 2 s:
  // generic rules arrive after a round trip to the browser.
  std::string WaitForDisplay(const content::ToRenderFrameHost& frame,
                             std::string_view id,
                             std::string_view expected) {
    return content::EvalJs(frame, content::JsReplace(R"(new Promise(done => {
               const start = performance.now();
               (function poll() {
                 const shown = getComputedStyle(document.getElementById($1)).display;
                 if (shown === $2 || performance.now() - start > 2000) {
                   done(shown);
                 } else {
                   requestAnimationFrame(poll);
                 }
               })();
             }))",
                                                     id, expected))
        .ExtractString();
  }
};

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, SiteRuleHidesBeforeThePageSeesIt) {
  // Alternating sites swaps renderer processes: the rules must still come first.
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/site.html")));
    EXPECT_EQ(content::EvalJs(Tab(), "window.seenAtParse"), "none");
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/site.html")));
    EXPECT_EQ(content::EvalJs(Tab(), "window.seenAtParse"), "block");
  }
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, GenericRulesHideByClassAndId) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/generic.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "generic", "none"), "none");
  EXPECT_EQ(WaitForDisplay(Tab(), "banner", "none"), "none");
  EXPECT_EQ(Display(Tab(), "plain"), "block");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, GenericRulesHideElementsAddedLater) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/page.html")));
  ASSERT_TRUE(content::ExecJs(Tab(), R"(
      document.body.insertAdjacentHTML('beforeend',
          '<div class="generic-ad" id="late">ad</div><div class="other" id="late-plain">x</div>');
  )"));
  EXPECT_EQ(WaitForDisplay(Tab(), "late", "none"), "none");
  // Both names went in the same batch, so this one has been answered too.
  EXPECT_EQ(Display(Tab(), "late-plain"), "block");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, AnExceptionShowsTheElementOnItsSite) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/generic.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "generic", "none"), "none");
  EXPECT_EQ(Display(Tab(), "allowed"), "block");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/generic.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "allowed", "none"), "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, GenerichideTurnsGenericRulesOff) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("nohide.test", "/generic.html")));
  // The site's own rule applies...
  EXPECT_EQ(Display(Tab(), "site"), "none");
  // ...the generic ones never do: no scanner runs. Give one the time it would
  // take (20 times the batch interval).
  ASSERT_TRUE(content::ExecJs(Tab(), "new Promise(done => setTimeout(done, 2000))"));
  EXPECT_EQ(Display(Tab(), "generic"), "block");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, OffHidesNothing) {
  privacy_policy::SetLevel(browser()->profile(), Url("a.test", "/"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/site.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.seenAtParse"), "block");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/generic.html")));
  ASSERT_TRUE(content::ExecJs(Tab(), "new Promise(done => setTimeout(done, 2000))"));
  EXPECT_EQ(Display(Tab(), "generic"), "block");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, IncognitoHides) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, Url("a.test", "/site.html")));
  EXPECT_EQ(content::EvalJs(Tab(incognito), "window.seenAtParse"), "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, ACrossSiteFrameHasItsOwnRules) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page.html")));
  content::TestNavigationObserver observer(Tab());
  ASSERT_TRUE(content::ExecJs(
      Tab(), content::JsReplace("const f = document.createElement('iframe');"
                                "f.src = $1; document.body.appendChild(f);",
                                Url("c.test", "/framed.html"))));
  observer.Wait();
  content::RenderFrameHost* frame = content::ChildFrameAt(Tab(), 0);
  ASSERT_TRUE(frame);
  EXPECT_EQ(Display(frame, "frame-ad"), "none");
  // a.test's rule is the page's site's, not the frame's.
  EXPECT_EQ(Display(frame, "site"), "block");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, AFrameItsParentWritesTakesTheParentsRules) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/written.html")));
  EXPECT_EQ(content::EvalJs(Tab(),
                            "getComputedStyle(document.getElementById('f').contentDocument"
                            ".getElementById('inner')).display"),
            "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, ThePageSeesNothingOfUs) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/override.html")));
  // A user !important declaration beats the page's.
  EXPECT_EQ(Display(Tab(), "site"), "none");
  EXPECT_EQ(content::EvalJs(Tab(), "typeof shadeCosmeticFilters"), "undefined");
  // Only the page's own <style>.
  EXPECT_EQ(content::EvalJs(Tab(), "document.styleSheets.length"), 1);
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, AStrictCspDoesntStopTheScanner) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/csp.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "generic", "none"), "none");
}

IN_PROC_BROWSER_TEST_F(CosmeticFiltersBrowserTest, ALongClassNameDoesntEndTheRenderer) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("b.test", "/page.html")));
  ASSERT_TRUE(content::ExecJs(Tab(), R"(
      const d = document.createElement('div');
      d.id = 'long';
      d.className = 'x'.repeat(300) + ' generic-ad';
      document.body.appendChild(d);
  )"));
  EXPECT_EQ(WaitForDisplay(Tab(), "long", "none"), "none");
  EXPECT_FALSE(Tab()->IsCrashed());
}

}  // namespace
}  // namespace ghost::cosmetic_filters
```

`content::EvalJs` runs in the main world through the test harness, which CSP doesn't restrict. Add `#include "base/no_destructor.h"`.
- [ ] **Step 2: Run them.** Run `CosmeticFiltersBrowserTest.*` on the hidden desktop, three times (`--gtest_repeat=3`). Expected: every test passes with no retry. A flaky `SiteRuleHidesBeforeThePageSeesIt` means the ordering isn't guaranteed: stop and report to the user.
- [ ] **Step 3: The other suites.** Run all of `ghost_unittests` and `ghost_browsertests`. Expected: all pass with no retry. The request tests run on an engine that now holds cosmetic rules too.
- [ ] **Step 4: Commit:** `cosmetic_filters: browser tests`.

---

### Task 9: Fuzzer and performance tests

**Files:**
- Create: `components/blocking/fuzz/blocking_cosmetic_fuzzer.cc`
- Modify: `components/blocking/BUILD.gn`, `components/blocking/blocking_perftest.cc`, `BUILD.gn` (`gn_all`)

- [ ] **Step 1: The fuzzer.** `fuzz/blocking_cosmetic_fuzzer.cc`:

```cpp
// Cosmetic queries against the lists Shade ships, with arbitrary input: a page
// chooses its URL's path and its class and id names. The first line is a
// document URL; each further line is a name, used both as a class and as an
// id. The bridge takes any UTF-8.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/check.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "ghost/components/blocking/filter_lists.h"
#include "ghost/components/blocking/rust/lib.rs.h"

namespace {

ghost::blocking::Engine& ShippedEngine() {
  static rust::Box<ghost::blocking::Engine>* engine = [] {
    std::string lists;
    for (const std::string& list :
         ghost::blocking::ReadFilterLists(ghost::blocking::DefaultFilterListsDir())) {
      lists += list;
      lists += '\n';
    }
    CHECK(!lists.empty()) << "the build copies the lists to <out>/blocking";
    return new rust::Box<ghost::blocking::Engine>(ghost::blocking::new_engine(lists));
  }();
  return **engine;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  if (!base::IsStringUTF8AllowingNoncharacters(input)) {
    return 0;
  }
  const std::vector<std::string_view> lines =
      base::SplitStringPiece(input, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  ghost::blocking::Engine& engine = ShippedEngine();
  const ghost::blocking::CosmeticResources resources =
      engine.cosmetic_resources(rust::Str(lines[0].data(), lines[0].size()));
  rust::Vec<rust::String> names;
  for (size_t i = 1; i < lines.size(); ++i) {
    names.push_back(rust::String(lines[i].data(), lines[i].size()));
  }
  engine.generic_selectors(names, names, resources.exceptions);
  return 0;
}
```

In `components/blocking/BUILD.gn`:

```gn
fuzzer_test("ghost_blocking_cosmetic_fuzzer") {
  sources = [ "fuzz/blocking_cosmetic_fuzzer.cc" ]
  deps = [
    ":blocking",
    "//base",
    "//ghost/components/blocking/rust:engine",
  ]
  dict = "fuzz/filter_list.dict"
}
```

Add `"//ghost/components/blocking:ghost_blocking_cosmetic_fuzzer"` to `gn_all` in `BUILD.gn`. Build it in `out\vanilla` (a no-op stub outside fuzzing builds). Then in `out\fuzz` run `autoninja -C out\fuzz ghost_blocking_cosmetic_fuzzer`, and fuzz for 30 minutes with `-max_total_time=1800`, seeded with a corpus dir holding `https://a.test/page\nad-banner\nsponsored`. Expected: no crash. Record executions and coverage in the notes.
- [ ] **Step 2: The performance tests.** In `blocking_perftest.cc`:
  - Factor `std::string ShippedLists()` out of `ChecksWithinBudget` (the concatenation loop) and use it there.
  - Add the two tests below. Add includes `<set>` and `base/strings/string_number_conversions.h`.

```cpp
constexpr base::TimeDelta kCosmeticBudgetP99 = base::Milliseconds(1);

// p50 and p99 of sorted times.
std::pair<base::TimeDelta, base::TimeDelta> Percentiles(std::vector<base::TimeDelta> times) {
  std::sort(times.begin(), times.end());
  return {times[times.size() / 2], times[times.size() * 99 / 100]};
}

// The rules a document gets at its commit (Phase 4A's budget: p99 under 1 ms).
TEST(BlockingPerfTest, CosmeticRulesWithinBudget) {
  rust::Box<Engine> engine = new_engine(ShippedLists());
  std::set<std::string> pages;
  for (const Row& row : ReadCorpus()) {
    pages.insert(row.page);
  }
  std::vector<base::TimeDelta> times;
  size_t largest = 0;
  for (int pass = 0; pass < 100; ++pass) {
    for (const std::string& page : pages) {
      const base::TimeTicks start = base::TimeTicks::Now();
      const CosmeticResources resources = engine->cosmetic_resources(page);
      times.push_back(base::TimeTicks::Now() - start);
      largest = std::max(largest, resources.hide_selectors.size());
    }
  }
  const auto [p50, p99] = Percentiles(std::move(times));
  LOG(INFO) << "Cosmetic rules: " << pages.size() << " pages, at most " << largest
            << " selectors; p50 " << p50.InMicrosecondsF() << " us, p99 "
            << p99.InMicrosecondsF() << " us (budget " << kCosmeticBudgetP99.InMicroseconds()
            << " us).";
#if defined(OFFICIAL_BUILD)
  EXPECT_LT(p99, kCosmeticBudgetP99);
#endif
}

// A batch as a heavy page sends it: 1,000 names, 50 of them listed.
TEST(BlockingPerfTest, GenericSelectorsWithinBudget) {
  const std::string lists = ShippedLists();
  rust::Box<Engine> engine = new_engine(lists);
  rust::Vec<rust::String> classes;
  for (std::string_view line :
       base::SplitStringPiece(lists, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (classes.size() == 50) {
      break;
    }
    // A generic rule on one class, ##.name.
    if (line.starts_with("##.") &&
        line.find_first_of(" :[>+~.#", 3) == std::string_view::npos) {
      classes.push_back(rust::String(line.substr(3).data(), line.size() - 3));
    }
  }
  ASSERT_EQ(classes.size(), 50u);
  rust::Vec<rust::String> ids;
  for (int i = 0; i < 950; ++i) {
    ids.push_back(rust::String("element-" + base::NumberToString(i)));
  }
  const rust::Vec<rust::String> no_exceptions;
  std::vector<base::TimeDelta> times;
  size_t matched = 0;
  for (int pass = 0; pass < 1000; ++pass) {
    const base::TimeTicks start = base::TimeTicks::Now();
    matched = engine->generic_selectors(classes, ids, no_exceptions).size();
    times.push_back(base::TimeTicks::Now() - start);
  }
  const auto [p50, p99] = Percentiles(std::move(times));
  LOG(INFO) << "Generic selectors: 1000 names, " << matched << " matched; p50 "
            << p50.InMicrosecondsF() << " us, p99 " << p99.InMicrosecondsF() << " us (budget "
            << kCosmeticBudgetP99.InMicroseconds() << " us).";
  EXPECT_GT(matched, 0u);
#if defined(OFFICIAL_BUILD)
  EXPECT_LT(p99, kCosmeticBudgetP99);
#endif
}
```

  The existing `ChecksWithinBudget` now reports compile time and memory with every rule type.
- [ ] **Step 3: Run them.** Run `autoninja -C out\vanilla ghost_blocking_perftests`, then `out\vanilla\ghost_blocking_perftests.exe`. Expected: pass (no budget in a dev build); record the numbers as dev-build numbers.
- [ ] **Step 4: Commit:** `blocking: a cosmetic fuzzer and performance tests`.

---

### Task 10: The budgets in the official build

- [ ] **Step 1: The fixture.** `test/cosmetic/heavy.html` (MPL header in an HTML comment, plain indented lines):

```html
<!doctype html>
<title>5,000 elements</title>
<body>
<script>
  // What a heavy page gives the scanner at once: 5,000 elements with 1,007
  // distinct classes and 5,000 ids.
  let html = '';
  for (let i = 0; i < 5000; ++i) {
    html += `<div class="c${i % 1000} d${i % 7}" id="e${i}">x</div>`;
  }
  document.body.innerHTML = html;
</script>
</body>
```

- [ ] **Step 2: The measuring script.** `tools/scanner_cost.py`, with the trace event names from the spike notes (Task 1, Step 9):
  - It takes `--chrome <exe>` and `--runs N` (default 5).
  - It serves `test/cosmetic` with `http.server` on a free port in a thread.
  - It launches the browser twice, each with a fresh `--user-data-dir`, `--no-first-run` and `--remote-debugging-port`. The second profile's `Default/Preferences` is written before launch with `{"ghost": {"privacy_policy": {"site_levels": {"127.0.0.1": "off"}}}}`, so its loads have no rules and no scanner.
  - For each run, through `tools/cdp.py`, it reads `Performance.getMetrics` before and after loading the page (and one more second for later batches). It prints the medians of `ScriptDuration` and `TaskDuration` in each profile and their difference, the scanner's cost.
  - It stops only the processes of its own profiles (their command line names the profile directory).

  Stdlib only, as `tools/cdp.py`. The arithmetic is pure functions (`metric_deltas(before, after)`, `medians(rows)`, `off_preferences()`), tested in `tools/tests/test_scanner_cost.py` (OS-neutral paths), and run with `python -m unittest discover -s tools/tests -t tools`. It runs the browsers on the hidden desktop when given `--launcher <hidden.ps1>`.
- [ ] **Step 3: Ask the user.** The official build takes hours, and `out\release` belongs to the release workflow. Offer two options:
  - **(a)** `ghost_blocking_perftests` in `out\release`: minutes, decides the two engine budgets;
  - **(b)** also `chrome` in `out\release`: hours, for the scanner's budget.

  If the user declines (b): measure the scanner in `out\vanilla` (JavaScript isn't slowed like Rust there) and report it as a dev-build number.
- [ ] **Step 4: Run.** Launch through `hidden.ps1`, with `chrome/VERSION` untouched:
  - `autoninja -C out\release ghost_blocking_perftests`, then run them;
  - if approved, `autoninja -C out\release chrome` through WMI as in the release runbook, then `python tools\scanner_cost.py --chrome out\release\chrome.exe`.

  Expected:
  - cosmetic rules p99 under 1 ms;
  - generic batch p99 under 1 ms;
  - scanner median under 5 ms.

  Record every number and the engine's memory and compile time against 6 MiB and 80 ms in the progress notes. A missed budget: stop and report to the user (the spec allows changes only with their agreement).
- [ ] **Step 5: Commit:** `cosmetic_filters: the budgets, measured`.

---

### Task 11: Mutation checks, audit, docs, push

- [ ] **Mutation checks.** Each must make at least one test fail, and each mutant must build: check the build's success before trusting a red or green run. Undo each with `sync.py`, then compare `src/ghost` with webops.
  - **M1:** `CosmeticFiltersHost::Select` passes `{}` instead of `*exceptions_`. Expected to fail: `AnExceptionShowsTheElementOnItsSite`.
  - **M2:** the throttle ignores the level (the `kOff` branch removed). Expected to fail: `OffHidesNothing`.
  - **M3:** the agent never runs the scanner (`current_->scan` treated as false). Expected to fail: `GenericRulesHideByClassAndId`, `GenericRulesHideElementsAddedLater`.
  - **M4:** the host skips `AreValidNames`. Expected to fail: `TooManyNamesAreABadMessage`, `ANameThatIsNotUtf8IsABadMessage`.
  - **M5:** `IsSafe` returns `!selector.empty()`. Expected to fail: `StyleSheetTest.UnsafeSelectorsAreDropped`.
- [ ] **Full suites and the audit.** Run `ghost_unittests` and `ghost_browsertests` in full, the browser tests on the hidden desktop. Run the egress audit (`tools/egress` per `docs/testing.md`). Expected: 0 unexpected hosts.
- [ ] **Docs.**
  - `privacy-model.md`:
    - what each level hides (Off nothing; Standard and Strict the lists' site and generic rules);
    - the Strict sentence corrected;
    - the page can't see or override the hiding;
    - elements added by script can show until their batch is answered.
  - `architecture.md`: replace the Phase 4 "Cosmetic filtering and scriptlets" sketch with "as built (4A)": the throttle, the tab helper, the host, the agent, the scanner's world, patches 0041 and 0042, the inheritance of initial empty documents.
  - `threat-model.md`: a new renderer-to-browser interface: what it can ask, its limits, the bad-message rule; the selectors' sanitizing against updated lists.
  - `testing.md`: the cosmetic tests, the fuzzer, the budgets and `scanner_cost.py`.
  - `roadmap.md`: Phase 4 split into 4A–4D; 4A done, with numbers.
  - The progress notes; the status of the spec and of this plan.
- [ ] **Push.** Ask the user before pushing. After the push, wait for both tooling jobs to finish; then update the memory.
