# Breakage Controls (4D-2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When a site doesn't work, the protections panel lowers its level by one and reloads, asks whether that helped, and opens a pre-filled GitHub issue the user has seen first ([spec](../specs/2026-10-18-breakage-controls-design.md)).

**Architecture:**
- **What was blocked:** `PageProtections` keeps, per tab, the blocked requests (at most 50 distinct hosts) beside its count. The request and connection filters pass each blocked `CheckRequest`.
- **The guided step:** `BreakageCheck`, a `WebContentsUserData` in `//ghost/browser/protections`. It remembers the site's choice and a snapshot of the blocked requests, lowers the level, reloads, and says when the reload has loaded. The protections button reopens the panel then.
- **The report:** `breakage_report.{h,cc}`, pure functions: the fields, the address check, the issue URL.
  - Which rule blocked each request is found only when the report is asked for: `ExplainBlocks` builds a temporary adblock-rust engine with debug info from the shipped lists and checks the snapshot again. Nothing changes in the engine that filters requests.
  - `ListLabel` reads each list's `! Title:` and `! Version:`.
- **The panel:** `protections.mojom` gains `TryLowerLevel`, `KeepLevel`, `RestoreLevel`, `GetReport`, `OpenReport`. The page handler builds the URL in the browser and opens the tab. The Lit app gains three views: offer, ask, report.
- **The issue form:** `.github/ISSUE_TEMPLATE/breakage.yml`. A tooling test keeps its field ids equal to the ones C++ writes.
- No Chromium patch: everything lives in `//ghost`. The series stays at 43.

**Tech Stack:** C++ (content, views, WebUI, Mojo), Lit/TypeScript, Rust (adblock-rust 0.13.3 via cxx), gtest, Python `unittest`.

**Settled while planning** (each checked in the source; the spec is amended in Task 0):
- **The rule text needs an engine with debug info.** `new_engine` builds `FilterSet::new(false)`, so `BlockerResult.filter` has no `raw_line`, and `Decision.filter` carries only `FilterRuleDebugInfo`'s `Debug` form. A debug engine for every request would cost memory and check time; the request check's p99 is already 41–49 µs against a 50 µs budget (4A). So the rule is found at report time:
  - a temporary engine with `FilterSet::new(true)`, built on the thread pool from the same lists;
  - each snapshot request checked again, and the engine dropped.
  The report waits for one engine build (about a second).
- **The scriptlet names leave the report.** The engine returns a site's scriptlets as one script (`CosmeticResources.injected_script`), not by name. They follow from the address and the lists' versions, both in the report, so triage recomputes them. The spec's `scriptlets` field is dropped.
- **Every field of the issue form is an `input` or a `textarea`,** never a dropdown, so that each pre-fills from the URL. The category is written as its label. The form carries `labels: [breakage]` itself: the URL's `labels=` works only for people with triage rights.
- **The snapshot is taken before lowering.** After "Yes" the page is at the lower level, and its blocked requests are no longer the ones that broke it.
- **`RestoreLevel` needs the site's own choice,** not its effective level: `site_levels.h` gains `GetChoice`, which the existing private `SiteLevel` already computes.
- **The panel stays open across the reload,** as it does today for a level radio (`SetLevel` reloads and sends the state). The button reopens it only when it's closed.
- **A blocked alias counts as its alias URL.** `Decision.alias` holds the alias URL, and checking that URL again finds the rule.

**Process notes** (from earlier sub-projects):
- **Where to edit and build.** Edit in `webops`, copy into `chromium/src/ghost` with bash `cp` (never `Copy-Item`, which keeps the old mtime), and build there. Commit in `webops`. Session tools: scratchpad `sync.py`, `build.ps1`, `bt.ps1` (it sets `[Environment]::CurrentDirectory`), `hidden.ps1` (tests run on a hidden desktop). If they're gone, copy them from session `9d872102`'s scratchpad.
- **Builds:** `autoninja -C out\vanilla -j 10 ghost_unittests ghost_browsertests` from `chromium\src`, with `C:\src\depot_tools` first on `PATH`.
- **Never chain `git commit` after a test run** in one command.
- **Run `tools/lint.py` after `git add`:** it checks only tracked files. Write files with LF endings.
- **A mutant written as `if (false)` doesn't compile** (`-Wunreachable-code`, `/WX`), and the old binary then runs green. Check that each mutant's build succeeded.
- **Pull `src/ghost` back** after reverting a mutant, or compare it with `webops`: `git checkout` in `src/ghost` restores its HEAD, which may predate `webops`.

---

## File map

| File | Change |
|---|---|
| `components/blocking/rust/lib.rs` | `new_explaining_engine`; `check` returns `raw_line` when the engine has it |
| `components/blocking/explain.{h,cc}` (new) | `ExplainBlocks(lists, requests)`: the rule for each request |
| `components/blocking/filter_lists.{h,cc}` | `ListLabel(list)` |
| `components/blocking/engine_bridge_unittest.cc`, `filter_lists_unittest.cc`, `explain_unittest.cc` (new) | tests |
| `components/blocking/BUILD.gn` | `explain.*`, its test |
| `browser/blocking/blocking_service.{h,cc}` | `DescribeBlocks(requests, reply)`; keeps the test lists |
| `browser/blocking/request_filter.cc`, `connection_filter.cc` | pass the blocked `CheckRequest` |
| `browser/privacy_policy/site_levels.{h,cc}`, `site_levels_unittest.cc` | `GetChoice` |
| `browser/protections/page_protections.{h,cc}`, `page_protections_unittest.cc` | the blocked requests |
| `browser/protections/breakage_report.{h,cc}`, `breakage_report_unittest.cc` (new) | the report and its URL |
| `browser/protections/breakage_check.{h,cc}`, `breakage_check_unittest.cc` (new) | the guided step |
| `browser/protections/BUILD.gn`, root `BUILD.gn` | sources, deps, tests |
| `browser/ui/protections/protections.mojom` | the new calls and types |
| `browser/ui/protections/protections_page_handler.{h,cc}` | their implementation |
| `browser/ui/protections/protections_button.{h,cc}` | reopens the panel to ask |
| `browser/ui/protections/protections_strings.cc` | the new strings |
| `browser/ui/protections/BUILD.gn` | deps on `//ghost/browser/blocking`, `//ghost/version` |
| `browser/resources/protections/app.ts`, `app.html.ts`, `app.css` | the three views |
| `browser/ui/protections/protections_panel_browsertest.cc` | the flow's browser tests |
| `.github/ISSUE_TEMPLATE/breakage.yml` (new) | the issue form |
| `tools/tests/test_breakage_form.py` (new) | the form's ids against C++'s |
| docs | Task 13 |

---

### Task 0: Branch and the spec's planning amendments

**Files:**
- Modify: `docs/superpowers/specs/2026-10-18-breakage-controls-design.md`

- [ ] **Step 1: Branch**

```bash
git switch -c 4d-2
```

- [ ] **Step 2: Amend the spec's status line**

Replace the line `- Status: design approved 2026-10-18.` with:

```markdown
- Status: design approved 2026-10-18. Amended while planning ([plan](../plans/2026-10-18-breakage-controls.md)): the rule that blocked each request is found when the report is asked for, by a temporary engine with debug info built from the same lists (the filtering engine has none, and one for every request would cost memory and check time); the scriptlets' names leave the report, since they follow from the address and the lists' versions; every field of the issue form is an input or a text area, which pre-fill from the URL, and the form carries its label itself.
```

In the Decisions bullet "The report holds what triage needs", delete the line `  - the names of the scriptlets applied to the page.`. In Components, in `PageProtections`'s row, replace "keeps the page's blocked hosts, each with the first rule that blocked it (`Decision.filter`)" with "keeps the page's blocked requests, the first of each host (the rule is found when a report is asked for)", and delete the sentence `Also the names of the scriptlets the browser pushed to the page's main frame.` In the issue form's row, delete `` `scriptlets`, `` from the field ids. In Errors and limits, replace "**No blocked request and no scriptlet** on the page: the report says so in those words." with "**No blocked request** on the page: the report says so in those words." In Testing, replace "a report on a page with a tracker names its host and rule, and the scriptlets applied;" with "a report on a page with a tracker names its host and rule;".

- [ ] **Step 3: Lint and commit**

```bash
git add docs/superpowers/specs/2026-10-18-breakage-controls-design.md
python tools/lint.py
git commit -m "docs: 4D-2 design amended while planning"
```

---

### Task 1: An engine that names its rules

**Files:**
- Modify: `components/blocking/rust/lib.rs` (the `extern "Rust"` block; `new_engine`; `Engine::check`)
- Test: `components/blocking/engine_bridge_unittest.cc`

- [ ] **Step 1: Write the failing test** (append to `engine_bridge_unittest.cc`, inside the namespace)

```cpp
TEST(EngineBridgeTest, AnExplainingEngineNamesTheRule) {
  rust::Box<Engine> engine = new_explaining_engine("! a comment\n||tracker.test^$third-party\n");
  const Verdict verdict = engine->check("https://tracker.test/t.js", "https://a.test/", "script",
                                        "GET");
  EXPECT_TRUE(verdict.blocked);
  EXPECT_EQ(std::string(verdict.filter), "||tracker.test^$third-party");
}

TEST(EngineBridgeTest, AnExplainingEngineDecidesAsTheOtherOne) {
  constexpr char kLists[] = "||tracker.test^\n@@||tracker.test/ok.js\n";
  rust::Box<Engine> plain = new_engine(kLists);
  rust::Box<Engine> explaining = new_explaining_engine(kLists);
  for (const char* url : {"https://tracker.test/t.js", "https://tracker.test/ok.js",
                          "https://cdn.test/x.js"}) {
    EXPECT_EQ(Blocked(*plain, url, "https://a.test/"), Blocked(*explaining, url, "https://a.test/"))
        << url;
  }
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `autoninja -C out\vanilla -j 10 ghost_unittests`
Expected: compile error, `new_explaining_engine` is not declared.

- [ ] **Step 3: Implement**

In `lib.rs`'s `extern "Rust"` block, after `fn new_engine(lists: &str) -> Box<Engine>;`:

```rust
        /// As new_engine, but keeping each rule's text, which `check` then
        /// returns as the verdict's filter. For breakage reports (Phase 4D-2),
        /// built when one is asked for: it uses more memory than new_engine.
        fn new_explaining_engine(lists: &str) -> Box<Engine>;
```

Replace `fn new_engine(lists: &str) -> Box<Engine> {` and its body with:

```rust
fn new_engine(lists: &str) -> Box<Engine> {
    build_engine(lists, false)
}

fn new_explaining_engine(lists: &str) -> Box<Engine> {
    build_engine(lists, true)
}

/// `debug` keeps each rule's text in the engine (adblock-rust's debug mode).
fn build_engine(lists: &str, debug: bool) -> Box<Engine> {
    // The resolver is process-wide and set once; a second engine finds it set.
    let _ = set_domain_resolver(Box::new(ChromiumDomains));
    let mut filters = FilterSet::new(debug);
    // Every rule type: network rules for requests, cosmetic rules, scriptlets
    // and procedural rules for pages (Phases 4A, 4B).
    filters.add_filter_list(lists.to_string(), ParseOptions::default());
    let mut engine = adblock::Engine::new_with_filter_set(filters);
    engine.use_resources(resources::usable());
    let mut pages = FilterSet::new(debug);
    pages.add_filter_list(document_exceptions(lists), ParseOptions::default());
    Box::new(Engine(engine, adblock::Engine::new_with_filter_set(pages)))
}
```

In `Engine::check`, replace `filter: result.filter.map(|f| format!("{f:?}")).unwrap_or_default(),` with:

```rust
                    // The rule's text when the engine kept it (an explaining
                    // engine), otherwise what adblock-rust knows of it.
                    filter: result
                        .filter
                        .map(|f| f.raw_line.clone().unwrap_or_else(|| format!("{f:?}")))
                        .unwrap_or_default(),
```

- [ ] **Step 4: Run the tests**

Copy into `src/ghost`, then:
Run: `autoninja -C out\vanilla -j 10 ghost_unittests` and `out\vanilla\ghost_unittests.exe --gtest_filter=EngineBridgeTest.*`
Expected: every `EngineBridgeTest` passes, `AListedAliasBlocksTheRequest` too (its filter is still non-empty).

If `AnExplainingEngineNamesTheRule` shows a filter of the form `FilterRuleDebugInfo { raw_line: None, … }`, adblock-rust 0.13 drops `raw_line` even in debug mode for this kind of rule. Then read `third_party/rust/adblock/v0_13/crate/src/filters/network.rs` for where `raw_line` is set (`NetworkFilter::parse` with `debug`), and record the finding in the progress notes before going on.

- [ ] **Step 5: Commit**

```bash
git add components/blocking/rust/lib.rs components/blocking/engine_bridge_unittest.cc
python tools/lint.py
git commit -m "blocking: an engine that keeps its rules' text"
```

---

### Task 2: Explaining a page's blocks, and naming the lists

**Files:**
- Create: `components/blocking/explain.h`, `components/blocking/explain.cc`, `components/blocking/explain_unittest.cc`
- Modify: `components/blocking/filter_lists.h`, `components/blocking/filter_lists.cc`, `components/blocking/filter_lists_unittest.cc`, `components/blocking/BUILD.gn`

- [ ] **Step 1: Write the failing tests**

`components/blocking/explain_unittest.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/explain.h"

#include <string>
#include <vector>

#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::blocking {
namespace {

using network::mojom::RequestDestination;

CheckRequest Script(const char* url) {
  return {GURL(url), GURL("https://a.test/"), RequestDestination::kScript, "GET"};
}

TEST(ExplainTest, NamesTheRuleForEachRequest) {
  const std::vector<std::string> lists = {"||tracker.test^$third-party\n", "||ads.test/banner\n"};
  EXPECT_EQ(ExplainBlocks(lists, {Script("https://tracker.test/t.js"),
                                  Script("https://ads.test/banner.js")}),
            (std::vector<std::string>{"||tracker.test^$third-party", "||ads.test/banner"}));
}

TEST(ExplainTest, ARequestTheListsAllowHasNoRule) {
  EXPECT_EQ(ExplainBlocks({"||tracker.test^\n"}, {Script("https://cdn.test/x.js")}),
            (std::vector<std::string>{""}));
}

TEST(ExplainTest, WithoutListsNothingIsExplained) {
  EXPECT_EQ(ExplainBlocks({}, {Script("https://tracker.test/t.js")}),
            (std::vector<std::string>{""}));
}

}  // namespace
}  // namespace ghost::blocking
```

Append to `filter_lists_unittest.cc`, inside the namespace:

```cpp
TEST(FilterListsTest, AListIsNamedByItsTitleAndVersion) {
  EXPECT_EQ(ListLabel("[Adblock Plus 2.0]\n! Version: 202610100609\n! Title: EasyList\n||a^\n"),
            "EasyList 202610100609");
  EXPECT_EQ(ListLabel("! Title: Shade's fixes\n! a comment\n"), "Shade's fixes");
  EXPECT_EQ(ListLabel("||a.test^\n! Title: Too late\n"), "Untitled list");
}

TEST(FilterListsTest, TheShippedListsHaveNames) {
  std::vector<std::string> labels;
  for (const std::string& list : ReadFilterLists(DefaultFilterListsDir())) {
    labels.push_back(ListLabel(list));
  }
  ASSERT_EQ(labels.size(), 3u);
  EXPECT_TRUE(labels[0].starts_with("EasyList 2")) << labels[0];
  EXPECT_TRUE(labels[1].starts_with("EasyPrivacy 2")) << labels[1];
  EXPECT_EQ(labels[2], "Shade's fixes");
}
```

- [ ] **Step 2: Add them to the build and see them fail**

In `components/blocking/BUILD.gn`'s `unit_tests`, add `"explain_unittest.cc",` to `sources` (alphabetical) and `"//services/network/public/mojom:mojom_shared",` to `deps`.
Run: `autoninja -C out\vanilla -j 10 ghost_unittests`
Expected: compile errors, no `explain.h` and no `ListLabel`.

- [ ] **Step 3: Implement**

`components/blocking/explain.h`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_BLOCKING_EXPLAIN_H_
#define GHOST_COMPONENTS_BLOCKING_EXPLAIN_H_

#include <string>
#include <vector>

#include "ghost/components/blocking/blocking_engine.h"

namespace ghost::blocking {

// The rule of `lists` that blocks each request, or an empty string for one
// they allow: for breakage reports (Phase 4D-2). Builds an engine that keeps
// its rules' text, so it takes about as long as loading the lists: call it on
// a task that may block, and only when a report is asked for.
std::vector<std::string> ExplainBlocks(const std::vector<std::string>& lists,
                                       const std::vector<CheckRequest>& requests);

}  // namespace ghost::blocking

#endif  // GHOST_COMPONENTS_BLOCKING_EXPLAIN_H_
```

`components/blocking/explain.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/explain.h"

#include "base/strings/string_util.h"
#include "ghost/components/blocking/request_types.h"
#include "ghost/components/blocking/rust/lib.rs.h"

namespace ghost::blocking {

std::vector<std::string> ExplainBlocks(const std::vector<std::string>& lists,
                                       const std::vector<CheckRequest>& requests) {
  // As BlockingEngine::Load joins them: lists that aren't UTF-8 are left out
  // (cxx aborts on one), and each ends with a newline.
  std::string text;
  for (const std::string& list : lists) {
    if (!list.empty() && base::IsStringUTF8AllowingNoncharacters(list)) {
      text += list;
      text += '\n';
    }
  }
  std::vector<std::string> rules(requests.size());
  if (text.empty()) {
    return rules;
  }
  rust::Box<Engine> engine = new_explaining_engine(text);
  for (size_t i = 0; i < requests.size(); ++i) {
    const CheckRequest& request = requests[i];
    if (!request.url.is_valid() || !base::IsStringUTF8AllowingNoncharacters(request.method)) {
      continue;
    }
    const std::string page =
        request.source_url.is_valid() ? request.source_url.spec() : std::string();
    const Verdict verdict = engine->check(request.url.spec(), page,
                                          ToAdblockType(request.destination), request.method);
    if (verdict.blocked) {
      rules[i] = std::string(verdict.filter);
    }
  }
  return rules;
}

}  // namespace ghost::blocking
```

In `filter_lists.h`, after `ReadFilterLists`'s declaration:

```cpp
// A list as a breakage report names it: its "! Title:" and "! Version:"
// headers, "EasyList 202610100609"; the title alone without a version;
// "Untitled list" without a title. Only the header, the comment lines before
// the first rule, is read.
std::string ListLabel(std::string_view list);
```

(add `#include <string_view>` beside `<string>`). In `filter_lists.cc`, add `#include "base/strings/string_split.h"` and `#include "base/strings/string_util.h"`, and before the namespace's end:

```cpp
std::string ListLabel(std::string_view list) {
  std::string_view title, version;
  for (std::string_view line : base::SplitStringPiece(list, "\n", base::TRIM_WHITESPACE,
                                                      base::SPLIT_WANT_NONEMPTY)) {
    if (!line.starts_with('!') && !line.starts_with('[')) {
      break;
    }
    if (line.starts_with("! Title:")) {
      title = base::TrimWhitespaceASCII(line.substr(8), base::TRIM_ALL);
    } else if (line.starts_with("! Version:")) {
      version = base::TrimWhitespaceASCII(line.substr(10), base::TRIM_ALL);
    }
  }
  if (title.empty()) {
    return "Untitled list";
  }
  return version.empty() ? std::string(title) : base::StrCat({title, " ", version});
}
```

(also `#include "base/strings/strcat.h"`). In `components/blocking/BUILD.gn`'s `blocking` target, add `"explain.cc", "explain.h",` to `sources`.

- [ ] **Step 4: Run the tests**

Run: `autoninja -C out\vanilla -j 10 ghost_unittests` then `out\vanilla\ghost_unittests.exe --gtest_filter=ExplainTest.*:FilterListsTest.*`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add components/blocking/explain.h components/blocking/explain.cc components/blocking/explain_unittest.cc components/blocking/filter_lists.h components/blocking/filter_lists.cc components/blocking/filter_lists_unittest.cc components/blocking/BUILD.gn
python tools/lint.py
git commit -m "blocking: explain a page's blocks and name the lists"
```

---

### Task 3: The service describes blocks with the shipped lists

**Files:**
- Modify: `browser/blocking/blocking_service.h`, `browser/blocking/blocking_service.cc`

`DescribeBlocks` is tested through the panel's report in Task 9 (a browser test: the service is a process singleton started by the browser).

- [ ] **Step 1: Implement**

In `blocking_service.h`, add includes `<optional>`, `<string>`, `<vector>`, `"base/functional/callback.h"`, and in the class, after `engine()`:

```cpp
  // The rule that blocks each request and each list's label, from the lists
  // the engine was loaded with, for a breakage report (Phase 4D-2). Reads and
  // compiles the lists on the thread pool; replies on the calling sequence.
  using DescribeCallback = base::OnceCallback<void(std::vector<std::string> rules,
                                                   std::vector<std::string> list_labels)>;
  void DescribeBlocks(std::vector<CheckRequest> requests, DescribeCallback reply);
```

and in the private part, after `engine_`:

```cpp
  // SetListsForTesting's lists, which DescribeBlocks then uses instead of
  // the shipped ones.
  std::optional<std::vector<std::string>> lists_for_testing_;
```

In `blocking_service.cc`, add includes `"ghost/components/blocking/explain.h"`, `"ghost/components/blocking/filter_lists.h"` (if not there) and `"base/task/thread_pool.h"` (if not there), then:

```cpp
void BlockingService::SetListsForTesting(std::vector<std::string> lists) {
  lists_for_testing_ = lists;
  engine_.Load(std::move(lists));
}

void BlockingService::DescribeBlocks(std::vector<CheckRequest> requests, DescribeCallback reply) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](std::optional<std::vector<std::string>> lists, std::vector<CheckRequest> requests) {
            if (!lists) {
              lists = ReadFilterLists(DefaultFilterListsDir());
            }
            std::vector<std::string> labels;
            for (const std::string& list : *lists) {
              labels.push_back(ListLabel(list));
            }
            return std::make_pair(ExplainBlocks(*lists, requests), std::move(labels));
          },
          lists_for_testing_, std::move(requests)),
      base::BindOnce(
          [](DescribeCallback reply,
             std::pair<std::vector<std::string>, std::vector<std::string>> result) {
            std::move(reply).Run(std::move(result.first), std::move(result.second));
          },
          std::move(reply)));
}
```

(replace the old `SetListsForTesting` body).

- [ ] **Step 2: Build**

Run: `autoninja -C out\vanilla -j 10 ghost_browsertests`
Expected: builds.

- [ ] **Step 3: Commit**

```bash
git add browser/blocking/blocking_service.h browser/blocking/blocking_service.cc
python tools/lint.py
git commit -m "blocking: the service describes blocks for a report"
```

---

### Task 4: The tab keeps what was blocked

**Files:**
- Modify: `browser/protections/page_protections.h`, `page_protections.cc`, `page_protections_unittest.cc`, `browser/protections/BUILD.gn`
- Modify: `browser/blocking/request_filter.cc` (`OnStartVerdict`, `OnRedirectVerdict`, `Block`), `browser/blocking/connection_filter.cc` (`Judge`)
- Modify: `browser/protections/panel_state_unittest.cc` (its `RecordBlocked` call)

- [ ] **Step 1: Write the failing tests**

In `page_protections_unittest.cc`, add includes `"ghost/components/blocking/blocking_engine.h"` and `"services/network/public/mojom/fetch_api.mojom-shared.h"`, then in the anonymous namespace, before the fixture:

```cpp
blocking::CheckRequest Request(const char* url) {
  return {GURL(url), GURL("https://news.test/"),
          network::mojom::RequestDestination::kScript, "GET"};
}
```

Replace each `PageProtections::RecordBlocked(<frame>)` in this file with `PageProtections::RecordBlocked(<frame>, Request("https://t.test/x.js"))`, and add:

```cpp
TEST_F(PageProtectionsTest, KeepsTheFirstBlockedRequestOfEachHost) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://t.test/a.js"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://t.test/b.js"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://ads.test/c.js"));
  PageProtections* protections = PageProtections::FromWebContents(web_contents());
  ASSERT_EQ(protections->blocked_requests().size(), 2u);
  EXPECT_EQ(protections->blocked_requests()[0].url, GURL("https://t.test/a.js"));
  EXPECT_EQ(protections->blocked_requests()[1].url, GURL("https://ads.test/c.js"));
  EXPECT_EQ(protections->blocked_count(), 3);
  EXPECT_FALSE(protections->more_hosts());
}

TEST_F(PageProtectionsTest, KeepsAtMostFiftyHosts) {
  NavigateAndCommit(GURL("https://news.test/"));
  for (int i = 0; i < 60; ++i) {
    PageProtections::RecordBlocked(
        main_rfh()->GetGlobalId(),
        Request(base::StrCat({"https://t", base::NumberToString(i), ".test/x.js"}).c_str()));
  }
  PageProtections* protections = PageProtections::FromWebContents(web_contents());
  EXPECT_EQ(protections->blocked_requests().size(), PageProtections::kMaxBlockedHosts);
  EXPECT_TRUE(protections->more_hosts());
  EXPECT_EQ(protections->blocked_count(), 60);
}

TEST_F(PageProtectionsTest, ANavigationForgetsTheBlockedRequests) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://t.test/a.js"));
  NavigateAndCommit(GURL("https://other.test/"));
  PageProtections* protections = PageProtections::FromWebContents(web_contents());
  EXPECT_TRUE(protections->blocked_requests().empty());
  EXPECT_FALSE(protections->more_hosts());
}
```

(add `"base/strings/strcat.h"` and `"base/strings/string_number_conversions.h"` to the includes). In `panel_state_unittest.cc`, replace `PageProtections::RecordBlocked(main_rfh()->GetGlobalId());` with:

```cpp
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(),
                                 {GURL("https://t.test/x.js"), GURL("https://news.test/"),
                                  network::mojom::RequestDestination::kScript, "GET"});
```

(add `"ghost/components/blocking/blocking_engine.h"` and `"services/network/public/mojom/fetch_api.mojom-shared.h"` to its includes).

- [ ] **Step 2: See them fail**

Run: `autoninja -C out\vanilla -j 10 ghost_unittests`
Expected: compile errors in the tests (`RecordBlocked` takes one argument; no `blocked_requests`).

- [ ] **Step 3: Implement**

`page_protections.h`: add `#include <vector>` and `#include "ghost/components/blocking/blocking_engine.h"`. Replace `RecordBlocked`'s declaration and comment with:

```cpp
  // Most hosts a page keeps blocked requests for: a breakage report lists
  // them (Phase 4D-2).
  static constexpr size_t kMaxBlockedHosts = 50;

  // Counts one block for the tab whose primary page |frame| belongs to, and
  // keeps |request| when it is the first of its host and fewer than
  // kMaxBlockedHosts are kept; nothing when the frame is gone or its page
  // isn't primary (a page being left, a prerendered one). |request|'s URL is
  // the one blocked: a DNS alias's when an alias was.
  static void RecordBlocked(content::GlobalRenderFrameHostId frame,
                            blocking::CheckRequest request);

  // The page's blocked requests, the first of each host, for a breakage
  // report. They stay in the browser process.
  const std::vector<blocking::CheckRequest>& blocked_requests() const { return blocked_; }
  // Whether hosts beyond kMaxBlockedHosts were blocked.
  bool more_hosts() const { return more_hosts_; }
```

In the private part, after `blocked_count_`:

```cpp
  std::vector<blocking::CheckRequest> blocked_;
  bool more_hosts_ = false;
```

`page_protections.cc`: replace `RecordBlocked` and `PrimaryPageChanged`:

```cpp
// static
void PageProtections::RecordBlocked(content::GlobalRenderFrameHostId id,
                                    blocking::CheckRequest request) {
  content::RenderFrameHost* frame = content::RenderFrameHost::FromID(id);
  if (!frame) {
    return;
  }
  content::RenderFrameHost* main = frame->GetOutermostMainFrame();
  if (!main->GetPage().IsPrimary()) {
    return;
  }
  content::WebContents* contents = content::WebContents::FromRenderFrameHost(main);
  CreateForWebContents(contents);
  PageProtections* protections = FromWebContents(contents);
  const bool known = std::ranges::any_of(protections->blocked_, [&](const auto& kept) {
    return kept.url.host() == request.url.host();
  });
  if (!known) {
    if (protections->blocked_.size() < kMaxBlockedHosts) {
      protections->blocked_.push_back(std::move(request));
    } else {
      protections->more_hosts_ = true;
    }
  }
  protections->SetCount(protections->blocked_count_ + 1);
}
```

```cpp
void PageProtections::PrimaryPageChanged(content::Page& page) {
  blocked_.clear();
  more_hosts_ = false;
  SetCount(0);
}
```

(add `#include <algorithm>`). In `browser/protections/BUILD.gn`, add `"//ghost/components/blocking",` to `public_deps`.

`request_filter.cc`: change `Block` to take the URL that was blocked:

```cpp
  void Block(const GURL& url, const Decision& decision) {
    VLOG(1) << "Blocked " << url
            << (decision.alias.empty() ? "" : " (alias " + decision.alias + ")") << " by "
            << decision.filter;
    // A blocked alias counts as its own URL: a breakage report checks that
    // URL again to name the rule.
    protections::PageProtections::RecordBlocked(
        frame_, Check(decision.alias.empty() ? url : GURL(decision.alias)));
    if (decision.redirect.empty() || !Answer(decision.redirect)) {
      client_->OnComplete(network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
    }
    delete this;
  }
```

In `OnStartVerdict`, replace `Block(decision);` with `Block(request_.url, decision);`. In `OnRedirectVerdict`, replace `Block(decision);` with `Block(redirect_info.new_url, decision);`.

`connection_filter.cc`'s `Judge`: replace the `service->engine().Check(...)` call with:

```cpp
  CheckRequest request{url, page, network::mojom::RequestDestination::kEmpty, "GET"};
  service->engine().Check(
      request,
      base::BindOnce(
          [](content::GlobalRenderFrameHostId frame, CheckRequest request,
             base::OnceCallback<void(bool)> done, Decision decision) {
            if (decision.blocked) {
              protections::PageProtections::RecordBlocked(frame, std::move(request));
            }
            std::move(done).Run(decision.blocked);
          },
          frame, request, std::move(done)));
```

- [ ] **Step 4: Run the tests**

Run: `autoninja -C out\vanilla -j 10 ghost_unittests ghost_browsertests`, then `out\vanilla\ghost_unittests.exe --gtest_filter=PageProtectionsTest.*:PanelStateTest.*` and `bt.ps1` with `--gtest_filter=BlockedCountBrowserTest.*:ProtectionsPanelBrowserTest.*:ConnectionFilterBrowserTest.*:CnameUncloakingBrowserTest.*`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add browser/protections browser/blocking/request_filter.cc browser/blocking/connection_filter.cc
python tools/lint.py
git commit -m "protections: the tab keeps its page's blocked requests"
```

---

### Task 5: A site's own choice

**Files:**
- Modify: `browser/privacy_policy/site_levels.h`, `site_levels.cc`, `site_levels_unittest.cc`

- [ ] **Step 1: Write the failing test** (append to `site_levels_unittest.cc`, in its namespace, using its fixture; read the file's fixture name first, `SiteLevelsTest` in the plan below)

```cpp
TEST_F(SiteLevelsTest, AChoiceIsTheSitesOwn) {
  const GURL page("https://www.news.test/a");
  EXPECT_EQ(GetChoice(profile(), page), std::nullopt);
  SetLevel(profile(), page, ProtectionLevel::kStandard);
  EXPECT_EQ(GetChoice(profile(), page), ProtectionLevel::kStandard);
  EXPECT_EQ(GetChoice(profile(), GURL("https://news.test/")), ProtectionLevel::kStandard);
  ClearLevel(profile(), page);
  EXPECT_EQ(GetChoice(profile(), page), std::nullopt);
  EXPECT_EQ(GetChoice(profile(), GURL("about:blank")), std::nullopt);
}
```

If the fixture's profile accessor differs (for example a `TestingProfile profile_` member), use that.

- [ ] **Step 2: See it fail**

Run: `autoninja -C out\vanilla -j 10 ghost_unittests`
Expected: `GetChoice` not declared.

- [ ] **Step 3: Implement**

`site_levels.h`, after `GetLevel`:

```cpp
// The site's own choice, or nullopt when it has none and takes the mode's
// default (a breakage check restores exactly this, Phase 4D-2).
std::optional<ProtectionLevel> GetChoice(content::BrowserContext* context, const GURL& page);
```

(add `#include <optional>`). `site_levels.cc`, after `GetLevel`:

```cpp
std::optional<ProtectionLevel> GetChoice(content::BrowserContext* context, const GURL& page) {
  return SiteLevel(context, page);
}
```

- [ ] **Step 4: Run the test**

Run: `out\vanilla\ghost_unittests.exe --gtest_filter=SiteLevelsTest.*`
Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add browser/privacy_policy/site_levels.h browser/privacy_policy/site_levels.cc browser/privacy_policy/site_levels_unittest.cc
python tools/lint.py
git commit -m "privacy_policy: a site's own choice"
```

---

### Task 6: The report and its issue URL

**Files:**
- Create: `browser/protections/breakage_report.h`, `breakage_report.cc`, `breakage_report_unittest.cc`
- Modify: `browser/protections/BUILD.gn`, root `BUILD.gn` (`ghost_unittests` sources)

- [ ] **Step 1: Write the failing tests**

`browser/protections/breakage_report_unittest.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/breakage_report.h"

#include <string>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "net/base/url_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::protections {
namespace {

using privacy_policy::ProtectionLevel;

ReportInputs Inputs() {
  ReportInputs inputs;
  inputs.page = GURL("https://www.news.test/world/story?session=s3cr3t#top");
  inputs.level = ProtectionLevel::kStrict;
  inputs.tried = ProtectionLevel::kStandard;
  inputs.outcome = Outcome::kLowerHelped;
  inputs.version = "152.0.7977.169-1";
  inputs.lists = {"EasyList 202610100609", "EasyPrivacy 202610100609", "Shade's fixes"};
  inputs.blocked = {{"metrics.news.test", "||metrics.news.test^"},
                    {"tracker.test", "||tracker.test^$third-party"}};
  return inputs;
}

std::string Param(const GURL& url, std::string_view name) {
  std::string value;
  net::GetValueForKeyInQuery(url, name, &value);
  return value;
}

std::string Field(const BreakageReport& report, std::string_view id) {
  for (const ReportField& field : report.fields) {
    if (field.id == id) {
      return field.value;
    }
  }
  return "<missing>";
}

TEST(BreakageReportTest, TheAddressHasNoQueryOrFragment) {
  EXPECT_EQ(ReportAddress(GURL("https://www.news.test/world/story?session=s3cr3t#top")),
            "https://www.news.test/world/story");
  EXPECT_EQ(ReportAddress(GURL("http://news.test:8080/")), "http://news.test:8080/");
}

TEST(BreakageReportTest, AnEditedAddressMustStayOnTheSite) {
  const GURL page("https://www.news.test/world/story?x=1");
  EXPECT_EQ(CheckedAddress("https://www.news.test/world", page), "https://www.news.test/world");
  EXPECT_EQ(CheckedAddress("https://m.news.test/a?token=1#f", page), "https://m.news.test/a");
  EXPECT_EQ(CheckedAddress("https://evil.test/", page), "https://www.news.test/");
  EXPECT_EQ(CheckedAddress("javascript:alert(1)", page), "https://www.news.test/");
  EXPECT_EQ(CheckedAddress("", page), "https://www.news.test/");
}

TEST(BreakageReportTest, TheFieldsSayWhatHappened) {
  const BreakageReport report = BuildReport(Inputs());
  EXPECT_EQ(report.site, "news.test");
  EXPECT_EQ(report.address, "https://www.news.test/world/story");
  EXPECT_EQ(Field(report, "level"), "Strict");
  EXPECT_EQ(Field(report, "outcome"), "Standard fixed it");
  EXPECT_EQ(Field(report, "version"), "152.0.7977.169-1");
  EXPECT_EQ(Field(report, "lists"), "EasyList 202610100609, EasyPrivacy 202610100609, Shade's fixes");
  EXPECT_EQ(Field(report, "blocked"),
            "metrics.news.test  ||metrics.news.test^\ntracker.test  ||tracker.test^$third-party");
}

TEST(BreakageReportTest, EachOutcomeHasItsWords) {
  ReportInputs inputs = Inputs();
  inputs.outcome = Outcome::kLowerDidntHelp;
  EXPECT_EQ(Field(BuildReport(inputs), "outcome"), "Standard didn't fix it; back to Strict");
  inputs.outcome = Outcome::kAlreadyOff;
  inputs.level = ProtectionLevel::kOff;
  EXPECT_EQ(Field(BuildReport(inputs), "outcome"), "The site was already Off");
  inputs.outcome = Outcome::kNotTried;
  EXPECT_EQ(Field(BuildReport(inputs), "outcome"), "Not tried");
}

TEST(BreakageReportTest, NothingBlockedIsSaid) {
  ReportInputs inputs = Inputs();
  inputs.blocked.clear();
  EXPECT_EQ(Field(BuildReport(inputs), "blocked"), "Nothing was blocked on this page.");
  inputs.blocked = {{"tracker.test", ""}};
  EXPECT_EQ(Field(BuildReport(inputs), "blocked"), "tracker.test  (rule not found)");
  inputs.more_hosts = true;
  EXPECT_EQ(Field(BuildReport(inputs), "blocked"),
            "tracker.test  (rule not found)\n(more hosts were blocked)");
}

TEST(BreakageReportTest, TheIssueUrlCarriesEveryField) {
  const BreakageReport report = BuildReport(Inputs());
  const GURL url = IssueUrl(report, Category::kMedia, "The video stays black.",
                            "https://www.news.test/world");
  EXPECT_EQ(url.scheme(), "https");
  EXPECT_EQ(url.host(), "github.com");
  EXPECT_EQ(url.path(), "/robyroro/project-ghost/issues/new");
  EXPECT_EQ(Param(url, "template"), "breakage.yml");
  EXPECT_EQ(Param(url, "title"), "Breakage: news.test");
  EXPECT_EQ(Param(url, "site"), "https://www.news.test/world");
  EXPECT_EQ(Param(url, "level"), "Strict");
  EXPECT_EQ(Param(url, "outcome"), "Standard fixed it");
  EXPECT_EQ(Param(url, "version"), "152.0.7977.169-1");
  EXPECT_EQ(Param(url, "category"), "Video or audio");
  EXPECT_EQ(Param(url, "details"), "The video stays black.");
  EXPECT_EQ(Param(url, "blocked"),
            "metrics.news.test  ||metrics.news.test^\ntracker.test  ||tracker.test^$third-party");
  EXPECT_EQ(url.spec().find("s3cr3t"), std::string::npos);
}

TEST(BreakageReportTest, ALongListIsCutToFit) {
  ReportInputs inputs = Inputs();
  inputs.blocked.clear();
  for (int i = 0; i < 50; ++i) {
    inputs.blocked.push_back({base::StrCat({"host", base::NumberToString(i), ".test"}),
                              base::StrCat({"||host", base::NumberToString(i), ".test^",
                                            std::string(150, 'x')})});
  }
  const GURL url = IssueUrl(BuildReport(inputs), Category::kOther, "", "https://www.news.test/");
  EXPECT_LE(url.spec().size(), kMaxIssueUrlLength);
  const std::string blocked = Param(url, "blocked");
  EXPECT_TRUE(blocked.starts_with("host0.test")) << blocked;
  EXPECT_NE(blocked.find(" more"), std::string::npos) << blocked;
  EXPECT_TRUE(blocked.ends_with(" more")) << blocked;
}

}  // namespace
}  // namespace ghost::protections
```

Add `"browser/protections/breakage_report_unittest.cc",` to root `BUILD.gn`'s `ghost_unittests` sources (alphabetical, before `page_protections_unittest.cc`) and `"//net",` to its deps if absent.

- [ ] **Step 2: See them fail**

Run: `autoninja -C out\vanilla -j 10 ghost_unittests`
Expected: no `breakage_report.h`.

- [ ] **Step 3: Implement**

`browser/protections/breakage_report.h`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_PROTECTIONS_BREAKAGE_REPORT_H_
#define GHOST_BROWSER_PROTECTIONS_BREAKAGE_REPORT_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ghost/components/privacy_policy/protection_level.h"
#include "url/gurl.h"

namespace ghost::protections {

// What the guided step found (docs/superpowers/specs/2026-10-18-breakage-controls-design.md).
enum class Outcome { kNotTried, kAlreadyOff, kLowerHelped, kLowerDidntHelp };

// What broke, as the user chose it. The issue form receives its label.
enum class Category { kContent, kMedia, kSignIn, kOther };

// One field of the report, as the panel shows it and the issue form receives
// it: `id` is the form's field id (.github/ISSUE_TEMPLATE/breakage.yml).
struct ReportField {
  std::string id;
  std::string label;
  std::string value;
};

struct ReportInputs {
  GURL page;
  // The level the site broke at, and the one tried (meaningful with
  // kLowerHelped and kLowerDidntHelp).
  privacy_policy::ProtectionLevel level = privacy_policy::ProtectionLevel::kStandard;
  privacy_policy::ProtectionLevel tried = privacy_policy::ProtectionLevel::kOff;
  Outcome outcome = Outcome::kNotTried;
  std::string version;
  std::vector<std::string> lists;
  // Each blocked host with the rule that blocked it (empty if not found).
  std::vector<std::pair<std::string, std::string>> blocked;
  bool more_hosts = false;
};

struct BreakageReport {
  // The page's registrable domain, for the issue's title.
  std::string site;
  // ReportAddress(page): the panel shows it for the user to shorten.
  std::string address;
  // level, outcome, version, lists, blocked: shown read-only.
  std::vector<ReportField> fields;
};

// The issue URL's longest length; the blocked hosts are cut to fit.
inline constexpr size_t kMaxIssueUrlLength = 8000;

// The form's ids for the fields the user fills in the panel.
inline constexpr char kSiteFieldId[] = "site";
inline constexpr char kCategoryFieldId[] = "category";
inline constexpr char kDetailsFieldId[] = "details";

// |page| without its query, fragment or credentials.
std::string ReportAddress(const GURL& page);

// |edited| as ReportAddress when it is an http(s) URL on |page|'s registrable
// domain; otherwise |page|'s origin. The panel's renderer supplies |edited|.
std::string CheckedAddress(std::string_view edited, const GURL& page);

BreakageReport BuildReport(const ReportInputs& inputs);

// The category's words, as the panel and the issue show them.
std::string_view CategoryLabel(Category category);

// GitHub's new-issue page for the breakage form, filled in with |report|, the
// category, the user's details and the (checked) address.
GURL IssueUrl(const BreakageReport& report,
              Category category,
              std::string_view details,
              std::string_view address);

}  // namespace ghost::protections

#endif  // GHOST_BROWSER_PROTECTIONS_BREAKAGE_REPORT_H_
```

`browser/protections/breakage_report.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/breakage_report.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "ghost/components/site/registrable_domain.h"
#include "net/base/url_util.h"

namespace ghost::protections {

namespace {

using privacy_policy::ProtectionLevel;

constexpr char kIssueBase[] = "https://github.com/robyroro/project-ghost/issues/new";

std::string_view LevelWord(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::kOff:
      return "Off";
    case ProtectionLevel::kStandard:
      return "Standard";
    case ProtectionLevel::kStrict:
      return "Strict";
  }
}

std::string OutcomeWords(const ReportInputs& inputs) {
  switch (inputs.outcome) {
    case Outcome::kNotTried:
      return "Not tried";
    case Outcome::kAlreadyOff:
      return "The site was already Off";
    case Outcome::kLowerHelped:
      return base::StrCat({LevelWord(inputs.tried), " fixed it"});
    case Outcome::kLowerDidntHelp:
      return base::StrCat(
          {LevelWord(inputs.tried), " didn't fix it; back to ", LevelWord(inputs.level)});
  }
}

std::string BlockedLines(const std::vector<std::pair<std::string, std::string>>& blocked,
                         bool more_hosts) {
  if (blocked.empty()) {
    return "Nothing was blocked on this page.";
  }
  std::vector<std::string> lines;
  for (const auto& [host, rule] : blocked) {
    lines.push_back(base::StrCat({host, "  ", rule.empty() ? "(rule not found)" : rule}));
  }
  if (more_hosts) {
    lines.push_back("(more hosts were blocked)");
  }
  return base::JoinString(lines, "\n");
}

GURL WithParams(const BreakageReport& report,
                std::string_view blocked,
                Category category,
                std::string_view details,
                std::string_view address) {
  GURL url(kIssueBase);
  url = net::AppendQueryParameter(url, "template", "breakage.yml");
  url = net::AppendQueryParameter(url, "title", base::StrCat({"Breakage: ", report.site}));
  url = net::AppendQueryParameter(url, kSiteFieldId, address);
  for (const ReportField& field : report.fields) {
    url = net::AppendQueryParameter(url, field.id, field.id == "blocked" ? blocked : field.value);
  }
  url = net::AppendQueryParameter(url, kCategoryFieldId, CategoryLabel(category));
  return net::AppendQueryParameter(url, kDetailsFieldId, details);
}

}  // namespace

std::string ReportAddress(const GURL& page) {
  GURL::Replacements drop;
  drop.ClearQuery();
  drop.ClearRef();
  drop.ClearUsername();
  drop.ClearPassword();
  return page.ReplaceComponents(drop).spec();
}

std::string CheckedAddress(std::string_view edited, const GURL& page) {
  const GURL url(edited);
  if (url.is_valid() && url.SchemeIsHTTPOrHTTPS() && url.has_host() &&
      RegistrableDomain(url.host()) == RegistrableDomain(page.host())) {
    return ReportAddress(url);
  }
  return page.DeprecatedGetOriginAsURL().spec();
}

BreakageReport BuildReport(const ReportInputs& inputs) {
  BreakageReport report;
  report.site = std::string(RegistrableDomain(inputs.page.host()));
  report.address = ReportAddress(inputs.page);
  report.fields = {
      {"level", "Level", std::string(LevelWord(inputs.level))},
      {"outcome", "One level lower", OutcomeWords(inputs)},
      {"version", "Shade", inputs.version},
      {"lists", "Lists", base::JoinString(inputs.lists, ", ")},
      {"blocked", "Blocked on the page", BlockedLines(inputs.blocked, inputs.more_hosts)},
  };
  return report;
}

std::string_view CategoryLabel(Category category) {
  switch (category) {
    case Category::kContent:
      return "Page content missing or broken";
    case Category::kMedia:
      return "Video or audio";
    case Category::kSignIn:
      return "Sign-in or forms";
    case Category::kOther:
      return "Something else";
  }
}

GURL IssueUrl(const BreakageReport& report,
              Category category,
              std::string_view details,
              std::string_view address) {
  std::string blocked;
  for (const ReportField& field : report.fields) {
    if (field.id == "blocked") {
      blocked = field.value;
    }
  }
  GURL url = WithParams(report, blocked, category, details, address);
  // Too long: drop whole lines from the end, saying how many.
  std::vector<std::string> lines =
      base::SplitString(blocked, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  size_t dropped = 0;
  while (url.spec().size() > kMaxIssueUrlLength && !lines.empty()) {
    lines.pop_back();
    ++dropped;
    const std::string cut = base::StrCat(
        {base::JoinString(lines, "\n"), lines.empty() ? "" : "\n", "+",
         base::NumberToString(dropped), " more"});
    url = WithParams(report, cut, category, details, address);
  }
  return url;
}

}  // namespace ghost::protections
```

(add `#include "base/strings/string_split.h"`). In `browser/protections/BUILD.gn`, add `"breakage_report.cc", "breakage_report.h",` to `sources` and `"//net",` to `deps`.

The `blocked` field's value in the URL test is checked through `IssueUrl`, the only writer of the URL; `WithParams` is internal.

- [ ] **Step 4: Run the tests**

Run: `autoninja -C out\vanilla -j 10 ghost_unittests` then `out\vanilla\ghost_unittests.exe --gtest_filter=BreakageReportTest.*`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add browser/protections/breakage_report.h browser/protections/breakage_report.cc browser/protections/breakage_report_unittest.cc browser/protections/BUILD.gn BUILD.gn
python tools/lint.py
git commit -m "protections: the breakage report and its issue URL"
```

---

### Task 7: The guided step

**Files:**
- Create: `browser/protections/breakage_check.h`, `breakage_check.cc`, `breakage_check_unittest.cc`
- Modify: `browser/protections/BUILD.gn`, root `BUILD.gn`

- [ ] **Step 1: Write the failing tests**

`browser/protections/breakage_check_unittest.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/breakage_check.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/test/navigation_simulator.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/protections/page_protections.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::protections {
namespace {

using privacy_policy::ProtectionLevel;

const GURL kPage("https://www.news.test/a");

class BreakageCheckTest : public ChromeRenderViewHostTestHarness {
 protected:
  BreakageCheck* Check() {
    BreakageCheck::CreateForWebContents(web_contents());
    return BreakageCheck::FromWebContents(web_contents());
  }

  // Commits the reload TryLowerLevel or RestoreLevel started, to the end of
  // its loading.
  void FinishReload() {
    auto reload = content::NavigationSimulator::CreateFromPending(web_contents()->GetController());
    reload->Commit();
  }

  bool HasChoice() {
    return privacy_policy::GetChoice(profile(), kPage).has_value();
  }
};

TEST_F(BreakageCheckTest, LowersOneLevelAndAsksOnceReloaded) {
  NavigateAndCommit(kPage);
  privacy_policy::SetLevel(profile(), kPage, ProtectionLevel::kStrict);
  int changes = 0;
  auto subscription = Check()->AddChangedCallback(base::BindRepeating([&] { ++changes; }));

  ASSERT_TRUE(Check()->TryLowerLevel());
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kReloading);
  EXPECT_EQ(Check()->from(), ProtectionLevel::kStrict);
  EXPECT_EQ(Check()->to(), ProtectionLevel::kStandard);
  // Standard is the default: the site's choice is cleared, not written.
  EXPECT_FALSE(HasChoice());

  FinishReload();
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kAsking);
  EXPECT_GE(changes, 2);
}

TEST_F(BreakageCheckTest, KeepingTheLevelHelped) {
  NavigateAndCommit(kPage);
  ASSERT_TRUE(Check()->TryLowerLevel());  // Standard, the default, to Off
  FinishReload();
  Check()->KeepLevel();
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kAnswered);
  EXPECT_EQ(Check()->outcome(), Outcome::kLowerHelped);
  EXPECT_EQ(privacy_policy::GetLevel(profile(), kPage), ProtectionLevel::kOff);
}

TEST_F(BreakageCheckTest, RestoringPutsTheExactChoiceBack) {
  NavigateAndCommit(kPage);
  // No choice: the site had the default, Standard.
  ASSERT_TRUE(Check()->TryLowerLevel());
  FinishReload();
  ASSERT_TRUE(HasChoice());  // Off was written
  Check()->RestoreLevel();
  FinishReload();
  EXPECT_FALSE(HasChoice());
  EXPECT_EQ(Check()->outcome(), Outcome::kLowerDidntHelp);
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kAnswered);

  // An explicit Strict comes back as Strict.
  privacy_policy::SetLevel(profile(), kPage, ProtectionLevel::kStrict);
  Check()->Done();
  ASSERT_TRUE(Check()->TryLowerLevel());
  FinishReload();
  Check()->RestoreLevel();
  FinishReload();
  EXPECT_EQ(privacy_policy::GetChoice(profile(), kPage), ProtectionLevel::kStrict);
}

TEST_F(BreakageCheckTest, TheSnapshotIsTakenBeforeLowering) {
  NavigateAndCommit(kPage);
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(),
                                 {GURL("https://t.test/x.js"), kPage,
                                  network::mojom::RequestDestination::kScript, "GET"});
  ASSERT_TRUE(Check()->TryLowerLevel());
  FinishReload();
  ASSERT_EQ(Check()->blocked().size(), 1u);
  EXPECT_EQ(Check()->blocked()[0].url, GURL("https://t.test/x.js"));
}

TEST_F(BreakageCheckTest, AnOffSiteHasNothingLower) {
  NavigateAndCommit(kPage);
  privacy_policy::SetLevel(profile(), kPage, ProtectionLevel::kOff);
  EXPECT_FALSE(Check()->TryLowerLevel());
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kNone);
}

TEST_F(BreakageCheckTest, AnotherSiteEndsTheCheckAndKeepsTheLevel) {
  NavigateAndCommit(kPage);
  ASSERT_TRUE(Check()->TryLowerLevel());
  FinishReload();
  NavigateAndCommit(GURL("https://other.test/"));
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kNone);
  EXPECT_EQ(privacy_policy::GetLevel(profile(), kPage), ProtectionLevel::kOff);
}

TEST_F(BreakageCheckTest, NoStepWhereProtectionsDontApply) {
  NavigateAndCommit(GURL("chrome://no-such-page/"));
  EXPECT_FALSE(Check()->TryLowerLevel());
}

}  // namespace
}  // namespace ghost::protections
```

Add `"browser/protections/breakage_check_unittest.cc",` to `ghost_unittests` (alphabetical, first under `browser/protections/`).

- [ ] **Step 2: See them fail**

Run: `autoninja -C out\vanilla -j 10 ghost_unittests`
Expected: no `breakage_check.h`.

- [ ] **Step 3: Implement**

`browser/protections/breakage_check.h`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_PROTECTIONS_BREAKAGE_CHECK_H_
#define GHOST_BROWSER_PROTECTIONS_BREAKAGE_CHECK_H_

#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ghost/browser/protections/breakage_report.h"
#include "ghost/components/blocking/blocking_engine.h"
#include "ghost/components/privacy_policy/protection_level.h"

namespace ghost::protections {

// The protections panel's "Site not working?" step for a tab
// (docs/superpowers/specs/2026-10-18-breakage-controls-design.md): one level
// lower and a reload, then the user's answer, which a breakage report states.
// The step ends when the report is opened or the tab leaves the site; the
// lowered level stays then, since the user chose it.
class BreakageCheck : public content::WebContentsObserver,
                      public content::WebContentsUserData<BreakageCheck> {
 public:
  enum class Step {
    kNone,
    // The page reloads at the lower level.
    kReloading,
    // The reload has loaded: the panel asks whether the site works.
    kAsking,
    // The user answered; the report follows.
    kAnswered,
  };

  BreakageCheck(const BreakageCheck&) = delete;
  BreakageCheck& operator=(const BreakageCheck&) = delete;
  ~BreakageCheck() override;

  // Remembers the site's choice and the page's blocked requests, sets the
  // site one level lower and reloads. False, and nothing done, where
  // protections don't apply or the site is Off.
  bool TryLowerLevel();
  // The answers, while asking. Restoring writes the site's old choice back
  // (none, when it had none) and reloads.
  void KeepLevel();
  void RestoreLevel();
  // The report was opened: the step is over.
  void Done();

  Step step() const { return step_; }
  Outcome outcome() const { return outcome_; }
  privacy_policy::ProtectionLevel from() const { return from_; }
  privacy_policy::ProtectionLevel to() const { return to_; }
  // The page's blocked requests when the step began.
  const std::vector<blocking::CheckRequest>& blocked() const { return blocked_; }
  bool more_hosts() const { return more_hosts_; }

  // Runs after each change of step.
  base::CallbackListSubscription AddChangedCallback(base::RepeatingClosure callback);

 private:
  friend class content::WebContentsUserData<BreakageCheck>;

  explicit BreakageCheck(content::WebContents* contents);

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;
  void DidStopLoading() override;

  void SetStep(Step step);

  Step step_ = Step::kNone;
  Outcome outcome_ = Outcome::kNotTried;
  // The site the step is for, its choice before it, and the levels.
  std::string site_;
  std::optional<privacy_policy::ProtectionLevel> choice_before_;
  privacy_policy::ProtectionLevel from_ = privacy_policy::ProtectionLevel::kStandard;
  privacy_policy::ProtectionLevel to_ = privacy_policy::ProtectionLevel::kOff;
  std::vector<blocking::CheckRequest> blocked_;
  bool more_hosts_ = false;
  // The reload has committed: its end of loading asks.
  bool reloaded_ = false;
  base::RepeatingClosureList changed_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

// The level one below |level|: Strict to Standard, Standard to Off; none
// below Off.
std::optional<privacy_policy::ProtectionLevel> OneLower(privacy_policy::ProtectionLevel level);

}  // namespace ghost::protections

#endif  // GHOST_BROWSER_PROTECTIONS_BREAKAGE_CHECK_H_
```

`browser/protections/breakage_check.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/breakage_check.h"

#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/protections/page_protections.h"
#include "ghost/browser/protections/panel_state.h"
#include "ghost/components/site/registrable_domain.h"
#include "url/gurl.h"

namespace ghost::protections {

namespace {

using privacy_policy::ProtectionLevel;

GURL PageOf(content::WebContents& contents) {
  return contents.GetPrimaryMainFrame()->GetLastCommittedURL();
}

}  // namespace

std::optional<ProtectionLevel> OneLower(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::kStrict:
      return ProtectionLevel::kStandard;
    case ProtectionLevel::kStandard:
      return ProtectionLevel::kOff;
    case ProtectionLevel::kOff:
      return std::nullopt;
  }
}

BreakageCheck::BreakageCheck(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<BreakageCheck>(*contents) {}

BreakageCheck::~BreakageCheck() = default;

bool BreakageCheck::TryLowerLevel() {
  content::WebContents& contents = GetWebContents();
  const GURL page = PageOf(contents);
  if (!page.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  content::BrowserContext* context = contents.GetBrowserContext();
  const ProtectionLevel level = privacy_policy::GetLevel(context, page);
  const std::optional<ProtectionLevel> lower = OneLower(level);
  if (!lower) {
    return false;
  }
  site_ = std::string(RegistrableDomain(page.host()));
  choice_before_ = privacy_policy::GetChoice(context, page);
  from_ = level;
  to_ = *lower;
  outcome_ = Outcome::kNotTried;
  blocked_.clear();
  more_hosts_ = false;
  if (PageProtections* protections = PageProtections::FromWebContents(&contents)) {
    blocked_ = protections->blocked_requests();
    more_hosts_ = protections->more_hosts();
  }
  reloaded_ = false;
  SetStep(Step::kReloading);
  ChooseLevel(contents, *lower);  // reloads
  return true;
}

void BreakageCheck::KeepLevel() {
  if (step_ != Step::kAsking) {
    return;
  }
  outcome_ = Outcome::kLowerHelped;
  SetStep(Step::kAnswered);
}

void BreakageCheck::RestoreLevel() {
  if (step_ != Step::kAsking) {
    return;
  }
  content::WebContents& contents = GetWebContents();
  const GURL page = PageOf(contents);
  content::BrowserContext* context = contents.GetBrowserContext();
  if (choice_before_) {
    privacy_policy::SetLevel(context, page, *choice_before_);
  } else {
    privacy_policy::ClearLevel(context, page);
  }
  outcome_ = Outcome::kLowerDidntHelp;
  SetStep(Step::kAnswered);
  contents.GetController().Reload(content::ReloadType::NORMAL, /*check_for_repost=*/true);
}

void BreakageCheck::Done() {
  blocked_.clear();
  more_hosts_ = false;
  outcome_ = Outcome::kNotTried;
  SetStep(Step::kNone);
}

base::CallbackListSubscription BreakageCheck::AddChangedCallback(base::RepeatingClosure callback) {
  return changed_.Add(std::move(callback));
}

void BreakageCheck::PrimaryPageChanged(content::Page& page) {
  if (step_ == Step::kNone) {
    return;
  }
  const GURL url = page.GetMainDocument().GetLastCommittedURL();
  if (RegistrableDomain(url.host()) != site_) {
    Done();
    return;
  }
  reloaded_ = true;
}

void BreakageCheck::DidStopLoading() {
  if (step_ == Step::kReloading && reloaded_) {
    SetStep(Step::kAsking);
  }
}

void BreakageCheck::SetStep(Step step) {
  step_ = step;
  changed_.Notify();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(BreakageCheck);

}  // namespace ghost::protections
```

In `browser/protections/BUILD.gn`, add `"breakage_check.cc", "breakage_check.h",` to `sources`.

If `NavigationSimulator::CreateFromPending(...)->Commit()` doesn't fire `DidStopLoading` in this harness (the asking test then fails at `kReloading`), the reload is committed but loading isn't simulated as finished. Add `reload->SetKeepLoading(false);` before `Commit()`, or call `content::WebContentsTester::For(web_contents())->TestDidFinishLoad(kPage)` and listen to `DidFinishLoad` for the primary main frame instead of `DidStopLoading`. Record which one the harness needed in the progress notes.

- [ ] **Step 4: Run the tests**

Run: `autoninja -C out\vanilla -j 10 ghost_unittests` then `out\vanilla\ghost_unittests.exe --gtest_filter=BreakageCheckTest.*`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add browser/protections/breakage_check.h browser/protections/breakage_check.cc browser/protections/breakage_check_unittest.cc browser/protections/BUILD.gn BUILD.gn
python tools/lint.py
git commit -m "protections: the guided breakage step"
```

---

### Task 8: The panel's new calls

**Files:**
- Modify: `browser/ui/protections/protections.mojom`, `protections_page_handler.h`, `protections_page_handler.cc`, `browser/ui/protections/BUILD.gn`

- [ ] **Step 1: Extend the mojom**

In `protections.mojom`, after the `Level` enum:

```mojom
// Where the "Site not working?" step is (BreakageCheck::Step).
enum Step {
  kNone,
  kReloading,
  kAsking,
  kAnswered,
};

// What broke, as the user chose it in the report.
enum Category {
  kContent,
  kMedia,
  kSignIn,
  kOther,
};

// One line of the report, shown read-only.
struct ReportField {
  string label;
  string value;
};

// The report, before anything is sent.
struct Report {
  string site;
  // The page's address without query or fragment; the user may shorten it.
  string address;
  array<ReportField> fields;
};
```

In `State`, after `off_the_record`:

```mojom
  // The "Site not working?" step, and its levels: from, then one lower.
  Step step;
  Level check_from;
  Level check_to;
```

In `PageHandler`, after `OpenSettings();`:

```mojom
  // "Site not working?": the site one level lower, and a reload; the state's
  // step says when to ask.
  TryLowerLevel();
  // The answers to "Is it working now?".
  KeepLevel();
  RestoreLevel();
  // The report for the tab's page, as the panel shows it.
  GetReport() => (Report report);
  // Opens GitHub's form filled in with the last report, the category, the
  // user's details (at most 2,000 characters) and the address (at most
  // 2,048), which the browser checks; then closes the panel.
  OpenReport(Category category, string details, string address);
```

- [ ] **Step 2: Implement the handler**

`protections_page_handler.h`: add includes `<optional>`, `"base/callback_list.h"`, `"base/memory/weak_ptr.h"`, `"ghost/browser/protections/breakage_report.h"`; declare the overrides after `OpenSettings()`:

```cpp
  void TryLowerLevel() override;
  void KeepLevel() override;
  void RestoreLevel() override;
  void GetReport(GetReportCallback callback) override;
  void OpenReport(mojom::Category category,
                  const std::string& details,
                  const std::string& address) override;
```

and in the private part:

```cpp
  void OnReportDescribed(GURL page,
                         ReportInputs inputs,
                         GetReportCallback callback,
                         std::vector<std::string> rules,
                         std::vector<std::string> list_labels);

  // The last report the page was shown, which OpenReport sends; and the page
  // it was for.
  std::optional<BreakageReport> report_;
  GURL report_page_;
  base::CallbackListSubscription check_subscription_;
  base::WeakPtrFactory<ProtectionsPageHandler> weak_factory_{this};
```

`protections_page_handler.cc`: add includes `"ghost/browser/blocking/blocking_service.h"`, `"ghost/browser/protections/breakage_check.h"`, `"ghost/version/version.h"`, `"base/strings/string_util.h"`. In the anonymous namespace:

```cpp
constexpr size_t kMaxDetails = 2000;
constexpr size_t kMaxAddress = 2048;

mojom::Step ToMojo(BreakageCheck::Step step) {
  switch (step) {
    case BreakageCheck::Step::kNone:
      return mojom::Step::kNone;
    case BreakageCheck::Step::kReloading:
      return mojom::Step::kReloading;
    case BreakageCheck::Step::kAsking:
      return mojom::Step::kAsking;
    case BreakageCheck::Step::kAnswered:
      return mojom::Step::kAnswered;
  }
}

Category FromMojo(mojom::Category category) {
  switch (category) {
    case mojom::Category::kContent:
      return Category::kContent;
    case mojom::Category::kMedia:
      return Category::kMedia;
    case mojom::Category::kSignIn:
      return Category::kSignIn;
    case mojom::Category::kOther:
      return Category::kOther;
  }
}
```

(rename the existing `FromMojo(mojom::Level)` uses if overload resolution complains; both overloads can coexist). In `Follow`, after observing `PageProtections`:

```cpp
    BreakageCheck::CreateForWebContents(tab);
    check_subscription_ = BreakageCheck::FromWebContents(tab)->AddChangedCallback(
        base::BindRepeating(&ProtectionsPageHandler::SendState, base::Unretained(this)));
```

and at `Follow`'s start, after `protections_observation_.Reset();`, add `check_subscription_ = {};`. In `WebContentsDestroyed`, add `check_subscription_ = {};` before `Observe(nullptr);`.

In `State()`, before `return state;` at the end:

```cpp
  if (BreakageCheck* check = BreakageCheck::FromWebContents(web_contents())) {
    state->step = ToMojo(check->step());
    state->check_from = ToMojo(check->from());
    state->check_to = ToMojo(check->to());
  }
```

and in the early return for no `web_contents()`, set `state->check_from = mojom::Level::kStandard; state->check_to = mojom::Level::kOff;`.

The new methods:

```cpp
void ProtectionsPageHandler::TryLowerLevel() {
  if (web_contents()) {
    BreakageCheck::FromWebContents(web_contents())->TryLowerLevel();
  }
}

void ProtectionsPageHandler::KeepLevel() {
  if (web_contents()) {
    BreakageCheck::FromWebContents(web_contents())->KeepLevel();
  }
}

void ProtectionsPageHandler::RestoreLevel() {
  if (web_contents()) {
    BreakageCheck::FromWebContents(web_contents())->RestoreLevel();
  }
}

void ProtectionsPageHandler::GetReport(GetReportCallback callback) {
  blocking::BlockingService* service = blocking::BlockingService::GetIfStarted();
  if (!web_contents() || !service) {
    std::move(callback).Run(mojom::Report::New());
    return;
  }
  content::WebContents& tab = *web_contents();
  const GURL page = tab.GetPrimaryMainFrame()->GetLastCommittedURL();
  const PanelState panel = GetPanelState(tab);
  BreakageCheck* check = BreakageCheck::FromWebContents(&tab);
  PageProtections* protections = PageProtections::FromWebContents(&tab);

  ReportInputs inputs;
  inputs.page = page;
  inputs.version = DisplayVersion();
  std::vector<blocking::CheckRequest> requests;
  if (check && check->step() != BreakageCheck::Step::kNone) {
    inputs.level = check->from();
    inputs.tried = check->to();
    inputs.outcome = check->outcome();
    requests = check->blocked();
    inputs.more_hosts = check->more_hosts();
  } else {
    inputs.level = panel.level;
    inputs.outcome = panel.level == privacy_policy::ProtectionLevel::kOff ? Outcome::kAlreadyOff
                                                                          : Outcome::kNotTried;
    if (protections) {
      requests = protections->blocked_requests();
      inputs.more_hosts = protections->more_hosts();
    }
  }
  for (const blocking::CheckRequest& request : requests) {
    inputs.blocked.emplace_back(std::string(request.url.host()), std::string());
  }
  service->DescribeBlocks(
      std::move(requests),
      base::BindOnce(&ProtectionsPageHandler::OnReportDescribed, weak_factory_.GetWeakPtr(), page,
                     std::move(inputs), std::move(callback)));
}

void ProtectionsPageHandler::OnReportDescribed(GURL page,
                                               ReportInputs inputs,
                                               GetReportCallback callback,
                                               std::vector<std::string> rules,
                                               std::vector<std::string> list_labels) {
  for (size_t i = 0; i < inputs.blocked.size() && i < rules.size(); ++i) {
    inputs.blocked[i].second = std::move(rules[i]);
  }
  inputs.lists = std::move(list_labels);
  report_ = BuildReport(inputs);
  report_page_ = page;
  auto report = mojom::Report::New();
  report->site = report_->site;
  report->address = report_->address;
  for (const ReportField& field : report_->fields) {
    report->fields.push_back(mojom::ReportField::New(field.label, field.value));
  }
  std::move(callback).Run(std::move(report));
}

void ProtectionsPageHandler::OpenReport(mojom::Category category,
                                        const std::string& details,
                                        const std::string& address) {
  // The panel's renderer isn't trusted: it chooses words, never the URL.
  if (details.size() > kMaxDetails || address.size() > kMaxAddress ||
      !base::IsStringUTF8(details) || !base::IsStringUTF8(address)) {
    receiver_.ReportBadMessage("OpenReport: details or address too long or not UTF-8");
    return;
  }
  content::WebContents* tab = ActiveTab();
  if (!tab || !report_) {
    return;
  }
  const GURL url =
      IssueUrl(*report_, FromMojo(category), details, CheckedAddress(address, report_page_));
  tab->OpenURL(content::OpenURLParams(url, content::Referrer(),
                                      WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                      ui::PAGE_TRANSITION_LINK,
                                      /*is_renderer_initiated=*/false),
               /*navigation_handle_callback=*/{});
  if (BreakageCheck* check = BreakageCheck::FromWebContents(tab)) {
    check->Done();
  }
  report_.reset();
  ui_->CloseUI();
}
```

`browser/ui/protections/BUILD.gn`: add `"//ghost/browser/blocking",` and `"//ghost/version",` to the `protections` target's `deps`. If `gn check` reports a dependency cycle through `//chrome/browser:core` (which links `//ghost/browser/blocking`), the handler must get `DescribeBlocks` another way. In that case, move `DescribeBlocks`'s thread-pool work into a free function in `//ghost/components/blocking` (`DescribeShippedBlocks(requests, test_lists)`), which `ui/protections` may depend on. `BlockingService` then only exposes `lists_for_testing()`. Record the finding.

- [ ] **Step 3: Build**

Run: `autoninja -C out\vanilla -j 10 ghost_browsertests chrome`
Expected: builds (the panel's TypeScript still compiles: it ignores the new members).

- [ ] **Step 4: Commit**

```bash
git add browser/ui/protections
python tools/lint.py
git commit -m "protections: the panel's breakage calls"
```

---

### Task 9: The panel's views, strings and the button's reopening

**Files:**
- Modify: `browser/resources/protections/app.ts`, `app.html.ts`, `app.css`
- Modify: `browser/ui/protections/protections_strings.cc`
- Modify: `browser/ui/protections/protections_button.h`, `protections_button.cc`
- Test: `browser/ui/protections/protections_panel_browsertest.cc`

- [ ] **Step 1: Write the failing browser tests**

In `protections_panel_browsertest.cc`'s fixture, change `SetListsForTesting`'s list to `{"||tracker.test^$third-party\n||metrics.news.test^\n"}` (a first-party tracker of `news.test`: blocked at Strict, not checked at Standard). Add includes `"ghost/browser/protections/breakage_check.h"`, `"ghost/browser/privacy_policy/site_levels.h"` (if absent), `"content/public/test/test_navigation_observer.h"` (if absent), `"chrome/test/base/ui_test_utils.h"` (if absent), `"net/base/url_util.h"`. Add to the fixture:

```cpp
  // Clicks the element |selector| in the panel's shadow root.
  static bool ClickInPanel(content::WebContents* panel, std::string_view selector) {
    return content::ExecJs(
        panel, content::JsReplace("document.querySelector('protections-app').shadowRoot"
                                  ".querySelector($1).click()",
                                  selector));
  }

  // Waits until the element |selector| exists in the panel.
  static bool WaitForPanelElement(content::WebContents* panel, std::string_view selector) {
    return content::EvalJs(
               panel,
               content::JsReplace(
                   "new Promise(done => { const check = () => "
                   "document.querySelector('protections-app').shadowRoot.querySelector($1) ? "
                   "done(true) : setTimeout(check, 20); check(); })",
                   selector))
        .ExtractBool();
  }

  // Loads one script from |url| in the tab and waits until it has loaded or
  // failed.
  void LoadScript(Browser* in, const GURL& url) {
    EXPECT_EQ(content::EvalJs(Tab(in), content::JsReplace(R"(new Promise(done => {
                  const s = document.createElement('script');
                  s.onload = s.onerror = () => done('done');
                  s.src = $1;
                  document.head.append(s);
                }))",
                                                          url)),
              "done");
  }
```

Tests:

```cpp
IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, SiteNotWorkingLowersAndAsks) {
  privacy_policy::SetLevel(browser()->GetProfile(), Page("news.test"),
                           privacy_policy::ProtectionLevel::kStrict);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  LoadScript(browser(), Page("metrics.news.test"));
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  ASSERT_TRUE(ClickInPanel(panel, "#not-working"));
  EXPECT_TRUE(WaitForPanelText(panel, "#offer-reload", "Reload with Standard"));

  content::TestNavigationObserver reload(Tab(browser()));
  ASSERT_TRUE(ClickInPanel(panel, "#offer-reload"));
  reload.Wait();
  // The panel asks once the reload has loaded, reopened if it closed.
  ProtectionsButton* button = Button(browser());
  views::Widget* widget = button->bubble_manager_for_testing()->GetBubbleWidget();
  ASSERT_TRUE(widget);
  panel = button->bubble_manager_for_testing()->GetContentsWrapper()->web_contents();
  EXPECT_TRUE(WaitForPanelText(panel, "#ask-title", "Is it working now?"));
  EXPECT_EQ(privacy_policy::GetLevel(browser()->GetProfile(), Page("news.test")),
            privacy_policy::ProtectionLevel::kStandard);

  // No: Strict comes back, and the report shows what Strict blocked.
  content::TestNavigationObserver restore(Tab(browser()));
  ASSERT_TRUE(ClickInPanel(panel, "#ask-no"));
  restore.Wait();
  EXPECT_EQ(privacy_policy::GetChoice(browser()->GetProfile(), Page("news.test")),
            privacy_policy::ProtectionLevel::kStrict);
  EXPECT_TRUE(WaitForPanelElement(panel, "#report-open"));
  EXPECT_TRUE(WaitForPanelText(panel, "#report-site", "Report news.test"));
  const std::string fields = PanelText(panel, "#report-fields");
  EXPECT_NE(fields.find("Standard didn't fix it; back to Strict"), std::string::npos) << fields;
  EXPECT_NE(fields.find("metrics.news.test  ||metrics.news.test^"), std::string::npos) << fields;
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, TheReportOpensGitHubsFormAndNothingElse) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("www.news.test", "/page?session=s3cr3t")));
  content::WebContents* page_tab = Tab(browser());
  LoadImages(browser(), "tracker.test", 1);
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  ASSERT_TRUE(ClickInPanel(panel, "#not-working"));
  content::TestNavigationObserver reload(Tab(browser()));
  ASSERT_TRUE(ClickInPanel(panel, "#offer-reload"));
  reload.Wait();
  panel = Button(browser())->bubble_manager_for_testing()->GetContentsWrapper()->web_contents();
  ASSERT_TRUE(WaitForPanelElement(panel, "#ask-yes"));
  ASSERT_TRUE(ClickInPanel(panel, "#ask-yes"));
  ASSERT_TRUE(WaitForPanelElement(panel, "#report-open"));
  ASSERT_TRUE(content::ExecJs(panel, R"(
      const root = document.querySelector('protections-app').shadowRoot;
      root.querySelector('#report-category').value = '1';
      root.querySelector('#report-category').dispatchEvent(new Event('change'));
      root.querySelector('#report-details').value = 'The video stays black.';
      root.querySelector('#report-details').dispatchEvent(new Event('input'));)"));

  ui_test_utils::TabAddedWaiter added(browser());
  ASSERT_TRUE(ClickInPanel(panel, "#report-open"));
  content::WebContents* issue = added.Wait();
  const GURL url = issue->GetVisibleURL();
  EXPECT_EQ(url.host(), "github.com");
  EXPECT_EQ(url.path(), "/robyroro/project-ghost/issues/new");
  std::string value;
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "site", &value));
  EXPECT_EQ(value, embedded_test_server()->GetURL("www.news.test", "/page").spec());
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "category", &value));
  EXPECT_EQ(value, "Video or audio");
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "details", &value));
  EXPECT_EQ(value, "The video stays black.");
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "outcome", &value));
  EXPECT_EQ(value, "Off fixed it");
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "blocked", &value));
  EXPECT_EQ(value, "tracker.test  ||tracker.test^$third-party");
  EXPECT_EQ(url.spec().find("s3cr3t"), std::string::npos);
  // The report ends the step (the new tab is the active one now).
  EXPECT_EQ(BreakageCheck::FromWebContents(page_tab)->step(), BreakageCheck::Step::kNone);
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, AnOffSiteGoesStraightToTheReport) {
  privacy_policy::SetLevel(browser()->GetProfile(), Page("news.test"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  ASSERT_TRUE(ClickInPanel(panel, "#not-working"));
  ASSERT_TRUE(WaitForPanelElement(panel, "#report-open"));
  const std::string fields = PanelText(panel, "#report-fields");
  EXPECT_NE(fields.find("The site was already Off"), std::string::npos) << fields;
  EXPECT_NE(fields.find("Nothing was blocked on this page."), std::string::npos) << fields;
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, IncognitosReportStaysIncognito) {
  // Incognito's HTTPS-First is strict: an http: page would get its
  // interstitial (4A's finding).
  net::EmbeddedTestServer https(net::EmbeddedTestServer::TYPE_HTTPS);
  https.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  https.RegisterRequestHandler(base::BindRepeating(&ServePage));
  ASSERT_TRUE(https.Start());
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, https.GetURL("a.test", "/page")));
  content::WebContents* panel = OpenPanel(incognito);
  ASSERT_TRUE(panel);
  ASSERT_TRUE(ClickInPanel(panel, "#not-working"));
  content::TestNavigationObserver reload(Tab(incognito));
  ASSERT_TRUE(ClickInPanel(panel, "#offer-reload"));
  reload.Wait();
  panel = Button(incognito)->bubble_manager_for_testing()->GetContentsWrapper()->web_contents();
  ASSERT_TRUE(WaitForPanelElement(panel, "#ask-yes"));
  ASSERT_TRUE(ClickInPanel(panel, "#ask-yes"));
  ASSERT_TRUE(WaitForPanelElement(panel, "#report-open"));
  ui_test_utils::TabAddedWaiter added(incognito);
  ASSERT_TRUE(ClickInPanel(panel, "#report-open"));
  content::WebContents* issue = added.Wait();
  EXPECT_TRUE(issue->GetBrowserContext()->IsOffTheRecord());
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, ALongDetailsTextIsABadMessage) {
  // The panel's renderer is driven past its own limit (maxlength): the
  // browser must refuse what a compromised renderer could send.
  privacy_policy::SetLevel(browser()->GetProfile(), Page("news.test"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  ASSERT_TRUE(ClickInPanel(panel, "#not-working"));
  ASSERT_TRUE(WaitForPanelElement(panel, "#report-open"));
  content::RenderProcessHostBadIpcMessageWaiter bad(panel->GetPrimaryMainFrame()->GetProcess());
  ASSERT_TRUE(content::ExecJs(panel, R"(
      const root = document.querySelector('protections-app').shadowRoot;
      const details = root.querySelector('#report-details');
      details.removeAttribute('maxlength');
      details.value = 'x'.repeat(2001);
      details.dispatchEvent(new Event('input'));
      root.querySelector('#report-open').click();)"));
  EXPECT_TRUE(bad.Wait());
}
```

`RenderProcessHostBadIpcMessageWaiter` is in `content/public/test/browser_test_utils.h`. If `ui_test_utils::TabAddedWaiter::Wait()` returns nothing at the pin, take the new tab as `browser()->tab_strip_model()->GetActiveWebContents()` after it.

- [ ] **Step 2: See them fail**

Run: `autoninja -C out\vanilla -j 10 ghost_browsertests` then `bt.ps1 --gtest_filter=ProtectionsPanelBrowserTest.*`
Expected: the new tests fail (no `#not-working` element).

- [ ] **Step 3: Strings**

In `protections_strings.cc`'s `kPageStrings`, after `settingsLink`:

```cpp
    {"notWorking", "Site not working?"},
    {"offerText", "Try $1 on $2: the page reloads with fewer protections."},
    {"offerReload", "Reload with $1"},
    {"cancel", "Cancel"},
    {"askTitle", "Is it working now?"},
    {"askText", "$1 is at $2."},
    {"askNo", "No, put $1 back"},
    {"askYes", "Yes"},
    {"askNote", "Either way, you can tell us about the site."},
    {"reportTitle", "Report $1"},
    {"reportCategory", "What broke"},
    {"categoryContent", "Page content missing or broken"},
    {"categoryMedia", "Video or audio"},
    {"categorySignIn", "Sign-in or forms"},
    {"categoryOther", "Something else"},
    {"reportDetails", "What went wrong? (optional)"},
    {"reportAddress", "Address"},
    {"reportNote",
     "Opens a public issue on GitHub, filled in with the above. Nothing is sent until you "
     "submit it there."},
    {"reportOpen", "Open on GitHub"},
    {"reportLoading", "Preparing the report…"},
```

The category words must equal `CategoryLabel`'s; Task 11's test checks both against the form.

- [ ] **Step 4: The Lit app**

`app.ts`: add imports `import type {Report} from './protections.mojom-webui.js';` and `Category, Step` to the existing value import from `./protections.mojom-webui.js`. Add properties:

```ts
  static override get properties() {
    return {
      state_: {type: Object},
      view_: {type: String},
      report_: {type: Object},
      category_: {type: Number},
      details_: {type: String},
      address_: {type: String},
    };
  }

  protected accessor state_: State|null = null;
  // The panel's own view: the levels, the offer, or the report. The browser's
  // step decides "asking" (BreakageCheck).
  protected accessor view_: 'main'|'offer'|'report' = 'main';
  protected accessor report_: Report|null = null;
  protected accessor category_: Category = Category.kContent;
  protected accessor details_: string = '';
  protected accessor address_: string = '';
```

Methods (add before `onSettingsClick_`):

```ts
  protected asking_(): boolean {
    return !!this.state_ && this.state_.step === Step.kAsking;
  }

  protected levelName_(level: Level): string {
    switch (level) {
      case Level.kOff:
        return this.i18n_('levelOff');
      case Level.kStrict:
        return this.i18n_('levelStrict');
      default:
        return this.i18n_('levelStandard');
    }
  }

  // The level one lower, which the offer proposes.
  protected lower_(): Level {
    return this.state_?.level === Level.kStrict ? Level.kStandard : Level.kOff;
  }

  protected i18nF_(name: string, ...args: string[]): string {
    return loadTimeData.getStringF(name, ...args);
  }

  protected categories_(): Array<{value: Category, name: string}> {
    return [
      {value: Category.kContent, name: this.i18n_('categoryContent')},
      {value: Category.kMedia, name: this.i18n_('categoryMedia')},
      {value: Category.kSignIn, name: this.i18n_('categorySignIn')},
      {value: Category.kOther, name: this.i18n_('categoryOther')},
    ];
  }

  protected onNotWorkingClick_() {
    // Nothing lower to try at Off, nor once the user answered.
    if (this.isOff_() || this.state_?.step === Step.kAnswered) {
      this.showReport_();
    } else {
      this.view_ = 'offer';
    }
  }

  protected onOfferReload_() {
    this.view_ = 'main';
    ProtectionsBrowserProxy.getInstance().handler.tryLowerLevel();
  }

  protected onCancel_() {
    this.view_ = 'main';
    this.report_ = null;
  }

  protected onAskYes_() {
    ProtectionsBrowserProxy.getInstance().handler.keepLevel();
    this.showReport_();
  }

  protected onAskNo_() {
    ProtectionsBrowserProxy.getInstance().handler.restoreLevel();
    this.showReport_();
  }

  private async showReport_() {
    this.view_ = 'report';
    this.report_ = null;
    const {report} = await ProtectionsBrowserProxy.getInstance().handler.getReport();
    this.report_ = report;
    this.address_ = report.address;
    this.category_ = Category.kContent;
    this.details_ = '';
  }

  protected onCategoryChange_(e: Event) {
    this.category_ = Number((e.target as HTMLSelectElement).value) as Category;
  }

  protected onDetailsInput_(e: Event) {
    this.details_ = (e.target as HTMLTextAreaElement).value;
  }

  protected onAddressInput_(e: Event) {
    this.address_ = (e.target as HTMLInputElement).value;
  }

  protected onReportOpen_() {
    ProtectionsBrowserProxy.getInstance().handler.openReport(
        this.category_, this.details_, this.address_);
  }
```

The asking view needs the browser's step even when `view_` is `'report'`: `asking_()` wins in the template (below), and Yes/No move to the report.

`app.html.ts`: replace the template's body (keep the `applies_()` guard) with:

```ts
  return this.applies_() ? html`<!--_html_template_start_-->
<div id="header"><div id="site">${this.site_()}</div></div>
${this.asking_() ? html`
<div class="step">
  <div class="title" id="ask-title">${this.i18n_('askTitle')}</div>
  <p>${this.i18nF_('askText', this.site_(), this.levelName_(this.state_!.checkTo))}</p>
  <div class="buttons">
    <cr-button id="ask-no" @click="${this.onAskNo_}">
      ${this.i18nF_('askNo', this.levelName_(this.state_!.checkFrom))}
    </cr-button>
    <cr-button id="ask-yes" class="action-button" @click="${this.onAskYes_}">
      ${this.i18n_('askYes')}
    </cr-button>
  </div>
  <p class="note">${this.i18n_('askNote')}</p>
</div>` : this.view_ === 'offer' ? html`
<div class="step">
  <div class="title">${this.i18n_('notWorking')}</div>
  <p>${this.i18nF_('offerText', this.levelName_(this.lower_()), this.site_())}</p>
  <div class="buttons">
    <cr-button id="offer-cancel" @click="${this.onCancel_}">${this.i18n_('cancel')}</cr-button>
    <cr-button id="offer-reload" class="action-button" @click="${this.onOfferReload_}">
      ${this.i18nF_('offerReload', this.levelName_(this.lower_()))}
    </cr-button>
  </div>
</div>` : this.view_ === 'report' ? html`
<div class="step">
  <div class="title" id="report-site">
    ${this.i18nF_('reportTitle', this.report_ ? this.report_.site : this.site_())}
  </div>
  ${this.report_ ? html`
  <label class="field">${this.i18n_('reportCategory')}
    <select id="report-category" class="md-select" @change="${this.onCategoryChange_}">
      ${this.categories_().map(item => html`
      <option value="${item.value}" ?selected="${item.value === this.category_}">
        ${item.name}</option>`)}
    </select>
  </label>
  <textarea id="report-details" rows="2" maxlength="2000"
      placeholder="${this.i18n_('reportDetails')}" .value="${this.details_}"
      @input="${this.onDetailsInput_}"></textarea>
  <label class="field">${this.i18n_('reportAddress')}
    <input id="report-address" type="url" maxlength="2048" .value="${this.address_}"
        @input="${this.onAddressInput_}">
  </label>
  <div id="report-fields">${this.report_.fields.map(field => html`
    <div class="report-field"><span class="label">${field.label}</span>
      <span class="value">${field.value}</span></div>`)}</div>
  <p class="note">${this.i18n_('reportNote')}</p>
  <div class="buttons">
    <cr-button id="report-cancel" @click="${this.onCancel_}">${this.i18n_('cancel')}</cr-button>
    <cr-button id="report-open" class="action-button" @click="${this.onReportOpen_}">
      ${this.i18n_('reportOpen')}
    </cr-button>
  </div>` : html`<p class="note">${this.i18n_('reportLoading')}</p>`}
</div>` : html`
${this.isOff_() ? html`
<div class="summary">
  <span class="big off">${this.i18n_('levelOff')}</span>
  <span class="label">${this.i18n_('offLabel')}</span>
</div>` : html`
<div class="summary">
  <span class="big" id="count">${this.blockedCount_()}</span>
  <span class="label">${this.countLabel_()}</span>
</div>`}
<div id="levels" role="radiogroup" aria-label="${this.i18n_('levelGroup')}">
  ${this.levels_().map(item => html`
  <label>
    <input type="radio" name="level" value="${item.level}"
        .checked="${this.isChosen_(item.level)}" @change="${this.onLevelChange_}">
    ${item.name}${this.isDefault_(item.level) ? html`<span class="default-mark">
      · ${this.i18n_('defaultMark')}</span>` : nothing}
  </label>`)}
</div>
<p id="about">${this.about_()}</p>
<cr-button id="not-working" @click="${this.onNotWorkingClick_}">
  ${this.i18n_('notWorking')}
</cr-button>`}
${this.offTheRecord_() ?
    html`<div id="note">${this.i18n_('incognitoNote')}</div>` : nothing}
<div id="footer">
  <a id="settings" href="#" @click="${this.onSettingsClick_}">
    ${this.i18n_('settingsLink')}
  </a>
</div>
<!--_html_template_end_-->` : nothing;
```

In `app.ts`, import `cr-button`: `import '//resources/cr_elements/cr_button/cr_button.js';`, and `md_select.css` if the select styling needs it (`//resources/cr_elements/md_select.css.js`, added to `styles`). Check `browser/resources/protections/BUILD.gn`'s `ts_deps` lists `//ui/webui/resources/cr_elements:build_ts`; add it if absent. Each `PanelText` reads `textContent.trim()`, which for `cr-button` includes the slot text, so `#offer-reload` reads "Reload with Standard".

`app.css`, append:

```css
#not-working {
  display: flex;
  margin: 0 16px 14px;
  width: calc(100% - 32px);
}

.step {
  padding: 0 16px 14px;
}

.step .title {
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 6px;
}

.step p {
  margin: 0 0 10px;
}

.step .note {
  color: var(--color-sys-on-surface-subtle);
  font-size: 12px;
}

.buttons {
  display: flex;
  gap: 8px;
  justify-content: flex-end;
  margin-bottom: 10px;
}

.field {
  display: flex;
  flex-direction: column;
  font-size: 12px;
  gap: 4px;
  margin-bottom: 8px;
}

#report-details,
#report-address {
  box-sizing: border-box;
  font: inherit;
  margin-bottom: 8px;
  width: 100%;
}

#report-fields {
  background: var(--color-sys-surface-container);
  border-radius: 8px;
  font-family: monospace;
  font-size: 11.5px;
  margin-bottom: 8px;
  max-height: 160px;
  overflow: auto;
  padding: 8px 10px;
  white-space: pre-wrap;
  word-break: break-all;
}

.report-field .label {
  color: var(--color-sys-on-surface-subtle);
  display: block;
  font-family: system-ui;
}
```

- [ ] **Step 5: The button reopens the panel to ask**

`protections_button.h`: include `"base/callback_list.h"`; in the private part, add `void OnCheckChanged();` and `base::CallbackListSubscription check_subscription_;`. `protections_button.cc`: include `"ghost/browser/protections/breakage_check.h"`. In `Follow`, after `protections_observation_.Reset();` add `check_subscription_ = {};`, and inside `if (tab) {`, after observing `PageProtections`:

```cpp
    BreakageCheck::CreateForWebContents(tab);
    check_subscription_ = BreakageCheck::FromWebContents(tab)->AddChangedCallback(
        base::BindRepeating(&ProtectionsButton::OnCheckChanged, base::Unretained(this)));
```

and:

```cpp
// The reload of "Site not working?" has loaded: the panel asks, reopened if
// the reload closed it.
void ProtectionsButton::OnCheckChanged() {
  BreakageCheck* check = web_contents() ? BreakageCheck::FromWebContents(web_contents()) : nullptr;
  if (check && check->step() == BreakageCheck::Step::kAsking &&
      !bubble_manager_->GetBubbleWidget()) {
    ShowPanel();
  }
}
```

In `WebContentsDestroyed`, `Follow(nullptr)` already resets the subscription.

- [ ] **Step 6: Run the tests**

Run: `autoninja -C out\vanilla -j 10 ghost_browsertests` then `bt.ps1 --gtest_filter=ProtectionsPanelBrowserTest.*`
Expected: all pass, the old ones too.

- [ ] **Step 7: Commit**

```bash
git add browser/resources/protections browser/ui/protections
python tools/lint.py
git commit -m "protections: the panel's Site not working? flow"
```

---

### Task 10: The issue form

**Files:**
- Create: `.github/ISSUE_TEMPLATE/breakage.yml`, `tools/tests/test_breakage_form.py`

- [ ] **Step 1: Write the failing test** (`tools/tests/test_breakage_form.py`)

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The breakage issue form has exactly the fields Shade fills in (Phase 4D-2)."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FORM = ROOT / ".github" / "ISSUE_TEMPLATE" / "breakage.yml"
REPORT = ROOT / "browser" / "protections" / "breakage_report.cc"
REPORT_H = ROOT / "browser" / "protections" / "breakage_report.h"
STRINGS = ROOT / "browser" / "ui" / "protections" / "protections_strings.cc"


def form_ids() -> list[str]:
    return re.findall(r"^\s+id:\s*(\w+)\s*$", FORM.read_text(encoding="utf-8"), re.M)


def written_ids() -> set[str]:
    # BuildReport's fields, then the three the panel fills in.
    report = REPORT.read_text(encoding="utf-8")
    ids = set(re.findall(r'^\s+\{"(\w+)", "[^"]*", ', report, re.M))
    ids |= set(re.findall(r'Field(?:Id)?\[\] = "(\w+)"', REPORT_H.read_text(encoding="utf-8")))
    return ids


class BreakageFormTest(unittest.TestCase):
    def test_the_form_has_the_fields_shade_writes(self):
        self.assertEqual(sorted(form_ids()), sorted(written_ids()))
        self.assertEqual(len(form_ids()), len(set(form_ids())))

    def test_every_field_can_be_filled_from_the_url(self):
        text = FORM.read_text(encoding="utf-8")
        self.assertNotIn("type: dropdown", text)
        self.assertNotIn("type: checkboxes", text)

    def test_the_form_labels_its_issues(self):
        self.assertRegex(FORM.read_text(encoding="utf-8"), r"(?m)^labels:\s*\[\s*breakage\s*\]")

    def test_the_categories_are_the_panels(self):
        labels = re.findall(r'return "([^"]+)";', REPORT.read_text(encoding="utf-8"))
        strings = STRINGS.read_text(encoding="utf-8")
        for name in ("Page content missing or broken", "Video or audio", "Sign-in or forms",
                     "Something else"):
            self.assertIn(name, labels)
            self.assertIn(f'"{name}"', strings)
            self.assertIn(name, FORM.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: See it fail**

Run: `python -m unittest tools.tests.test_breakage_form -v` (from the repository root; or `python -m unittest discover -s tools/tests -t tools -p test_breakage_form.py`)
Expected: FAIL, no `breakage.yml`.

- [ ] **Step 3: Write the form**

`.github/ISSUE_TEMPLATE/breakage.yml`:

```yaml
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The breakage report Shade's protections panel fills in
# (docs/superpowers/specs/2026-10-18-breakage-controls-design.md). Every field
# is an input or a text area, so the panel's URL can fill it; the ids are
# browser/protections/breakage_report.*'s (tools/tests/test_breakage_form.py).
name: A site doesn't work in Shade
description: Shade's protections panel fills this in. Check it, then submit.
title: "Breakage: "
labels: [breakage]
body:
  - type: markdown
    attributes:
      value: >-
        This report is public. Shade filled it in from the page you reported;
        remove anything you don't want to share before submitting.
  - type: input
    id: site
    attributes:
      label: Page
      description: The page's address, without its query or fragment.
    validations:
      required: true
  - type: input
    id: category
    attributes:
      label: What broke
      description: >-
        Page content missing or broken, Video or audio, Sign-in or forms, or
        Something else.
  - type: textarea
    id: details
    attributes:
      label: What went wrong
  - type: input
    id: level
    attributes:
      label: Protection level
  - type: input
    id: outcome
    attributes:
      label: One level lower
  - type: input
    id: version
    attributes:
      label: Shade version
  - type: input
    id: lists
    attributes:
      label: Filter lists
  - type: textarea
    id: blocked
    attributes:
      label: Blocked on the page
      description: Each blocked host, with the rule that blocked it.
      render: text
```

- [ ] **Step 4: Run the test**

Run: the command of Step 2, then the whole tooling suite `python -m unittest discover -s tools/tests -t tools`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add .github/ISSUE_TEMPLATE/breakage.yml tools/tests/test_breakage_form.py
python tools/lint.py
git commit -m "github: the breakage report form"
```

---

### Task 11: The user's look, and GitHub's form filled in

- [ ] **Step 1: Screenshots for the user**

Start `out\vanilla\chrome.exe` with a fresh `--user-data-dir` in the session's scratchpad, on `https://www.theguardian.com/world` at Strict (choose Strict in the panel). Go through the flow. Capture each state with the scratchpad's `shot.ps1` (PrintWindow) or the panel's DevTools (`--remote-debugging-port`, `Page.captureScreenshot` of the panel's target): the panel with the button, the offer, the question, the report. Show them to the user and wait for their approval of the look. Change the CSS as they ask.

- [ ] **Step 2: GitHub's form, filled in, not submitted**

`main` doesn't have `breakage.yml` yet, and GitHub reads templates from the default branch. So push the branch only after the user agrees (Task 13). Until then, check the form's shape against GitHub's syntax: open `https://github.com/robyroro/project-ghost/issues/new?template=breakage.yml` after the merge (Task 13, Step 4), from a report made in the dev build. Check that every field shows its value. Never press Submit; a real test issue only if the user asks for one.

---

### Task 12: Mutation checks, suites, audit

- [ ] **Step 1: Mutations** (each in `src/ghost`, build, run the named filter, see it FAIL, restore, rebuild; check that each mutant compiles)

| # | Mutation | Must fail |
|---|---|---|
| M1 | `ReportAddress`: delete `drop.ClearQuery();` | `BreakageReportTest.TheAddressHasNoQueryOrFragment`, `ProtectionsPanelBrowserTest.TheReportOpensGitHubsFormAndNothingElse` |
| M2 | `BreakageCheck::RestoreLevel`: replace the `if (choice_before_) … else …` with `privacy_policy::SetLevel(context, page, from_);` | `BreakageCheckTest.RestoringPutsTheExactChoiceBack` |
| M3 | `CheckedAddress`: delete the `RegistrableDomain(url.host()) == RegistrableDomain(page.host())` condition (`&& true` would warn; remove the clause and its `&&`) | `BreakageReportTest.AnEditedAddressMustStayOnTheSite` |
| M4 | `PageProtections::RecordBlocked`: `protections->blocked_.size() < kMaxBlockedHosts` → `true` (as `protections->blocked_.size() < 1000`) | `PageProtectionsTest.KeepsAtMostFiftyHosts` |
| M5 | `ProtectionsPageHandler::GetReport`: replace `requests = check->blocked();` with `requests = protections->blocked_requests();` (the report reads the reloaded page, not the snapshot) | `ProtectionsPanelBrowserTest.SiteNotWorkingLowersAndAsks` (the report must still name `metrics.news.test` after the reload) |
| M6 | `OpenReport`: delete the `details.size() > kMaxDetails ||` clause | `ProtectionsPanelBrowserTest.ALongDetailsTextIsABadMessage` |

Record each result in `docs/superpowers/specs/2026-10-18-breakage-controls-spike.md` (the progress notes, created here).

- [ ] **Step 2: Full suites**

Run: `ghost_unittests` and `ghost_browsertests` in full through `bt.ps1`, plus `python -m unittest discover -s tools/tests -t tools`.
Expected: everything passes with no retry. Record the counts: 168 unit tests + 25 new = 193 (in `out/vanilla`), 142 browser tests + 5 = 147.

- [ ] **Step 3: Egress audit**

Run: `python tools/egress_audit.py run --chrome chromium/src/out/vanilla/chrome.exe --netlog <scratchpad>/netlog.json` on the hidden desktop.
Expected: 0 unexpected hosts.

- [ ] **Step 4: Commit the notes**

```bash
git add docs/superpowers/specs/2026-10-18-breakage-controls-spike.md
python tools/lint.py
git commit -m "docs: 4D-2 mutation checks, suites, audit"
```

---

### Task 13: Documentation, merge, push

**Files:**
- Modify: `docs/privacy-model.md` (the "Breakage reports" bullet), `docs/architecture.md`, `docs/testing.md`, `docs/roadmap.md` (4D-2 and Phase 4), the spec's status, this plan's status

- [ ] **Step 1: The privacy model**

Replace `- **Breakage reports** are prepared on the user's machine and shown to the user before anything is sent. Nothing is reported automatically.` with:

```markdown
- **Breakage reports (Phase 4D-2).** The panel's *Site not working?* lowers the site's level by one and reloads; *Is it working now?* keeps the new level or puts the old one back. Then the panel shows the report:
  - the page's address without its query or fragment, which the user may shorten (the browser accepts only an address on the same site);
  - the level, and whether one level lower helped;
  - Shade's version and the lists' versions;
  - what broke, in the user's words;
  - the hosts blocked on the page, each with the rule that blocked it. The rule is found when the report is asked for, from the shipped lists.
  Nothing leaves before *Open on GitHub*. That button opens a new tab on GitHub's issue form for this repository, filled in through its URL. GitHub receives the contents then, before the user submits the issue, which is public. The blocked requests are kept in the browser process for the page's life, at most 50 hosts. Nothing is reported automatically.
```

- [ ] **Step 2: Architecture, testing, roadmap**

- **`architecture.md`:** under the protections panel, add a paragraph on `BreakageCheck`, `breakage_report`, `ExplainBlocks` and the issue form.
- **`testing.md`:** list the new suites: `BreakageReportTest`, `BreakageCheckTest`, `ExplainTest`, the panel's flow tests, `test_breakage_form.py`.
- **`roadmap.md`:** item 5 becomes 4D-2 done, with links, the checks' numbers and the limitations:
  - the report needs a GitHub account and is public;
  - the panel's step offers one level at a time;
  - Strict's 7 sites wait for reports.
  Also mark Phase 4 complete.

- [ ] **Step 3: Status lines, commit**

Set the spec's status to done with the date, and this plan's `**Status:**` line under the header. Then:

```bash
git add docs
python tools/lint.py
git commit -m "docs: breakage controls (4D-2) done"
```

- [ ] **Step 4: Merge and push (with the user's approval)**

```bash
git switch main
git merge --ff-only 4d-2
```

Ask the user before `git push origin main`. Then wait for both tooling jobs (Windows, Ubuntu) to finish green. Do Task 11 Step 2: check that GitHub's form is filled in from a dev-build report. Finally, `git branch -d 4d-2`, and reset `src/ghost` to `origin/main`.
