# Protections Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A toolbar shield shows how many requests Shade blocked on the current page and opens a WebUI bubble that sets the site's level ([spec](../specs/2026-10-10-protections-panel-design.md)).

**Architecture:** The blocking filters report each block against its frame to `PageProtections`, a per-tab counter of the primary page. `ProtectionsButton`, a `ToolbarButton` added by patch 0035, shows the count and opens `chrome://protections.top-chrome` through a `WebUIBubbleManager`. The page (TypeScript, Lit) talks to `ProtectionsPageHandler` over Mojo, which reads and writes the levels of 3D-1. Patch 0037 gives the request filter its frame; patch 0036 registers the page (config, Mojo binder, pak).

**Status:** Tasks 1–4 done 2026-10-10 ([progress notes](../specs/2026-10-10-protections-panel-spike.md)). The patches are numbered in commit order: the spike's came first, so 0035 is the toolbar, 0036 the page's registration, 0037 the request filter's frame.

**Tech Stack:** C++, TypeScript, Lit, Mojo, grit (`build_webui`), Chromium 152.0.7977.158 Views; `ChromeRenderViewHostTestHarness`, `InProcessBrowserTest`.

**Settled while planning** (each confirmed in the source at the pin; the spec is amended to match):
- **A third patch, 0036**, is needed. Three registrations live in Chromium's lists, with no hook:
  - the WebUI config in `chrome/browser/ui/webui/chrome_web_ui_configs.cc`;
  - the Mojo binder in `chrome/browser/chrome_browser_interface_binders_webui.cc`;
  - the page's pak in `chrome/chrome_paks.gni`, beside `tab_search_resources.pak`.

  The spec said the config needs no patch, but `//ghost/browser/startup` can't depend on UI code: `//chrome/browser/ui` depends on it.
- **Strings live in one C++ file**, `browser/ui/protections/protections_strings.{h,cc}`, English only. The page reads them through `loadTimeData`; the button uses them directly. A grd of messages would need its own locale paks, which means another patch to `chrome_repack_locales.gni`, for no translation yet. When translations come, the grd replaces this file.
- **UI code links into `//chrome/browser/ui`**:
  - `//ghost/browser/ui/protections` needs `ToolbarButton`, `WebUIBubbleManager` and `TopChromeWebUIController`, all in `//chrome/browser/ui`, which will depend on it (patch 0035).
  - It therefore lists only lower targets in `deps`, and patch 0035 adds it to `//chrome/browser/ui`'s `deps` and `allow_circular_includes_from`. This is Chromium's own pattern (`chrome/browser/ui/BUILD.gn:995`).
  - The counter, `//ghost/browser/protections`, depends on `//content` only, so the filters in `//chrome/browser:core` can use it.
- **The badge** comes from `IconWithBadgeImageSource` (`chrome/browser/ui/extensions`), the extensions' badge painter: the icon plus `Badge{text, text_color, background_color}`.
- **The icon** is `vector_icons::kShieldIcon` (`components/vector_icons/shield.icon`), plus our own `shield_off.icon`, the same shield with a slash.
- **The bubble's tab** is the active tab of the bubble's browser, through `webui::GetBrowserWindowInterface(web_ui()->GetWebContents())->GetTabStripModel()->GetActiveWebContents()`. `WebUIBubbleManager` closes the bubble when the active tab changes (`CloseBubbleOnTabActivationHelper`).
- **The model to copy** is `chrome://personal-context-notice`, the smallest top-chrome page:
  - `chrome/browser/ui/webui/personal_context/` (config, controller, handler, mojom);
  - `chrome/browser/resources/personal_context_notice/BUILD.gn` (`build_webui` with `mojo_files`).

---

### Task 1: Spike — an empty page end to end

The wiring is the uncertain part; prove all of it with a page that only says "Protections" before any logic.

**Files:**
- Create: `browser/ui/protections/BUILD.gn`, `protections_ui.{h,cc}`, `protections.mojom` (with only `PageHandler.ShowUI()`), `protections_button.{h,cc}` (a plain `ToolbarButton` with `kShieldIcon` that calls `ShowBubble`)
- Create: `browser/resources/protections/BUILD.gn`, `protections.html`, `app.ts`, `app.html.ts`, `browser_proxy.ts`
- Create: patches 0035 (toolbar) and 0036 (config, binder, pak) in `chromium/src`, `git commit -s` with `Why:` and `Upstream:` trailers
- Create: `docs/superpowers/specs/2026-10-10-protections-panel-spike.md` (progress notes)

- [x] **Step 1:** Write the targets on the personal-context-notice model. In `browser/resources/protections/BUILD.gn`:

```gn
import("//ui/webui/resources/tools/build_webui.gni")

build_webui("resources") {
  grd_prefix = "protections"
  static_files = [ "protections.html" ]
  ts_files = [ "app.ts", "app.html.ts", "browser_proxy.ts" ]
  mojo_files_deps = [ "//ghost/browser/ui/protections:mojo_bindings_ts__generator" ]
  mojo_files = [ "$root_gen_dir/ghost/browser/ui/protections/protections.mojom-webui.ts" ]
  ts_composite = true
  ts_deps = [
    "//third_party/lit/v3_0:build_ts",
    "//ui/webui/resources/cr_elements:build_ts",
    "//ui/webui/resources/js:build_ts",
    "//ui/webui/resources/mojo:build_ts",
  ]
  webui_context_type = "trusted"
}
```

  Find out where the pak lands (`$root_gen_dir/ghost/browser/resources/protections/protections_resources.pak` or `$root_gen_dir/chrome/...`); record the path for patch 0036.
- [x] **Step 2:** In `browser/ui/protections/BUILD.gn`, add `mojom("mojo_bindings") { sources = [ "protections.mojom" ]; webui_module_path = "/" }` and `source_set("protections")`.
  - Its `deps`: `//chrome/browser/ui/webui/top_chrome`, `//chrome/browser/ui/browser_window`, `//chrome/browser/profiles:profile`, `//ghost/browser/resources/protections:resources`, `//content/public/browser`, `//ui/views`, `//components/vector_icons`.
  - It has no dep on `//chrome/browser/ui`.
- [x] **Step 3:** Write patch 0035 in `chrome/browser/ui/views/toolbar/toolbar_view.cc`, right after the `if (location_bar_view) { ... } else { ... }` block in `Init()`:

```cpp
  // Shade: the protections button, right of the omnibox (patches/0035).
  if (display_mode_ == DisplayMode::kNormal) {
    AddChildView(ghost::protections::CreateProtectionsButton(browser_));
  }
```

  Also in patch 0035: `chrome/browser/ui/BUILD.gn` adds `"//ghost/browser/ui/protections"` to the `deps` of `source_set("ui")` and to its `allow_circular_includes_from`.
- [x] **Step 4:** Write patch 0036, with three changes:
  - `chrome_web_ui_configs.cc`: `map.AddWebUIConfig(std::make_unique<ghost::protections::ProtectionsUIConfig>());`.
  - `chrome_browser_interface_binders_webui.cc`, inside `PopulateChromeWebUIFrameBinders` under `#if !BUILDFLAG(IS_ANDROID)`: `ghost::protections::PopulateWebUIFrameBinders(map);`. That function lives in `protections_ui.cc` and calls `content::RegisterWebUIControllerInterfaceBinder<mojom::PageHandlerFactory, ProtectionsUI>(map)`.
  - `chrome_paks.gni`: the pak from Step 1 next to `tab_search_resources.pak`, with its `deps`.
- [x] **Step 5:** Sync and build (`sync.py`, then `autoninja -C out\vanilla -j 10 chrome`). Run `out\vanilla\chrome.exe`. Expected:
  - a shield right of the omnibox;
  - a click opens a bubble that says "Protections";
  - the second click opens it instantly (preloaded).
- [x] **Step 6:** `WebUIBubbleManager::Create` takes a string resource id: the task manager's name for the bubble's process (`IDS_ACCNAME_TAB_SEARCH` for Tab Search). Shade has no string resources. Find an upstream string that reads right ("Protections" or a neutral one); if none does, use the closest one and record it as a known wording issue in the progress notes, to fix when Shade's strings get a grd (with translations). Record the id chosen.
- [x] **Step 6b:** Check how a worker's factory arrives. Breakpoint or `LOG` in `WillCreateURLLoaderFactory` on a page that starts a dedicated worker. Record in the progress notes whether `frame` is the creator document or null, and the same for WebTransport's `frame_routing_id` from a worker.
- [x] **Step 7:** Record the build time, every surprise, and the pak path in the progress notes. Commit in webops: `protections: an empty panel end to end (patches 0035, 0036): spike`. Re-export the patches with `patches.py export`; `patches.py check` must pass.

### Task 2: `PageProtections`, the per-tab count

**Files:**
- Create: `browser/protections/BUILD.gn`, `browser/protections/page_protections.{h,cc}`
- Test: `browser/protections/page_protections_unittest.cc` (add to `ghost_unittests` in `BUILD.gn`)

- [x] **Step 1: Write the failing tests**

```cpp
class PageProtectionsTest : public ChromeRenderViewHostTestHarness {
 protected:
  int Count() {
    auto* protections = ghost::protections::PageProtections::FromWebContents(web_contents());
    return protections ? protections->blocked_count() : 0;
  }
};

TEST_F(PageProtectionsTest, CountsTheMainFrameAndSubframes) {
  NavigateAndCommit(GURL("https://news.test/"));
  ghost::protections::PageProtections::RecordBlocked(main_rfh()->GetGlobalId());
  content::RenderFrameHost* child =
      content::RenderFrameHostTester::For(main_rfh())->AppendChild("ad");
  child = content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("https://ads.test/frame"), child);
  ghost::protections::PageProtections::RecordBlocked(child->GetGlobalId());
  EXPECT_EQ(Count(), 2);
}

TEST_F(PageProtectionsTest, ANavigationResetsTheCount) {
  NavigateAndCommit(GURL("https://news.test/"));
  ghost::protections::PageProtections::RecordBlocked(main_rfh()->GetGlobalId());
  NavigateAndCommit(GURL("https://other.test/"));
  EXPECT_EQ(Count(), 0);
}

TEST_F(PageProtectionsTest, IgnoresFramesOfAnotherPage) {
  NavigateAndCommit(GURL("https://news.test/"));
  content::GlobalRenderFrameHostId old_page = main_rfh()->GetGlobalId();
  NavigateAndCommit(GURL("https://other.test/"));
  ghost::protections::PageProtections::RecordBlocked(old_page);
  ghost::protections::PageProtections::RecordBlocked(content::GlobalRenderFrameHostId());
  EXPECT_EQ(Count(), 0);
}

TEST_F(PageProtectionsTest, TellsItsObservers) {
  NavigateAndCommit(GURL("https://news.test/"));
  ghost::protections::PageProtections::CreateForWebContents(web_contents());
  auto* protections = ghost::protections::PageProtections::FromWebContents(web_contents());
  struct Recorder : ghost::protections::PageProtections::Observer {
    void OnBlockedCountChanged() override { ++calls; }
    int calls = 0;
  } recorder;
  base::ScopedObservation<ghost::protections::PageProtections,
                          ghost::protections::PageProtections::Observer>
      observation(&recorder);
  observation.Observe(protections);
  ghost::protections::PageProtections::RecordBlocked(main_rfh()->GetGlobalId());
  NavigateAndCommit(GURL("https://other.test/"));
  EXPECT_EQ(recorder.calls, 2);  // the block, then the reset
}
```

- [x] **Step 2: Run them to see them fail.** Run `autoninja -C out\vanilla ghost_unittests && out\vanilla\ghost_unittests --gtest_filter=PageProtections*`. Expected: a compile failure (no `page_protections.h`).
- [x] **Step 3: Implement.** `page_protections.h`:

```cpp
namespace ghost::protections {

// The requests and connections Shade blocked for the primary page of a tab,
// its subframes included (docs/privacy-model.md#blocking). A new primary page
// starts from zero. Shared and service workers aren't counted: no tab is theirs.
class PageProtections : public content::WebContentsObserver,
                        public content::WebContentsUserData<PageProtections> {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnBlockedCountChanged() = 0;
  };

  PageProtections(const PageProtections&) = delete;
  PageProtections& operator=(const PageProtections&) = delete;
  ~PageProtections() override;

  // Counts one block for the tab whose primary page |frame| belongs to;
  // nothing when the frame is gone or its page isn't primary (a page being
  // left, a prerendered one).
  static void RecordBlocked(content::GlobalRenderFrameHostId frame);

  int blocked_count() const { return blocked_count_; }
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  friend class content::WebContentsUserData<PageProtections>;
  explicit PageProtections(content::WebContents* contents);

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

  void SetCount(int count);

  int blocked_count_ = 0;
  base::ObserverList<Observer> observers_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace ghost::protections
```

  `page_protections.cc`:

```cpp
void PageProtections::RecordBlocked(content::GlobalRenderFrameHostId id) {
  content::RenderFrameHost* frame = content::RenderFrameHost::FromID(id);
  if (!frame) {
    return;
  }
  content::RenderFrameHost* main = frame->GetOutermostMainFrame();
  if (!main->GetPage().IsPrimary()) {
    return;
  }
  auto* contents = content::WebContents::FromRenderFrameHost(main);
  CreateForWebContents(contents);
  PageProtections* protections = FromWebContents(contents);
  protections->SetCount(protections->blocked_count_ + 1);
}

void PageProtections::PrimaryPageChanged(content::Page& page) { SetCount(0); }

void PageProtections::SetCount(int count) {
  blocked_count_ = count;
  for (Observer& observer : observers_) {
    observer.OnBlockedCountChanged();
  }
}
```

  `BUILD.gn`: `source_set("protections")` with `deps = [ "//base", "//content/public/browser" ]`. Header comment: "Linked into //chrome/browser:core through //ghost/browser/blocking: must not depend on //chrome/browser."
- [x] **Step 4: Run the tests.** Same command as Step 2. Expected: 4 passed.
- [x] **Step 5: Commit.** `protections: count a tab's blocked requests`.

### Task 3: The filters report their blocks (patch 0037)

**Files:**
- Modify: `browser/blocking/request_filter.{h,cc}`, `browser/blocking/connection_filter.cc`, `browser/blocking/BUILD.gn` (dep `//ghost/browser/protections`)
- Create: patch 0037 in `chromium/src` (`chrome/browser/chrome_content_browser_client.cc`)
- Test: `browser/protections/blocked_count_browsertest.cc` (add to `ghost_browsertests`)

- [x] **Step 1: Write the failing browser tests.** Reuse the fixtures of `request_filter_browsertest.cc` and `connection_filter_browsertest.cc`: the list `||tracker.test^$third-party`, `tracker.test` resolving to the test server, and the WebSocket server. Cases:
  - a page with three tracker images: `PageProtections::FromWebContents(tab)->blocked_count()` reaches 3 (wait with a `PageProtections::Observer` and `base::RunLoop`);
  - an iframe on `frame.test` loading one tracker image counts 1 for the tab;
  - a WebSocket to `tracker.test` counts 1;
  - a navigation to a page with no tracker gives 0;
  - a tracker page loaded in a background tab leaves the active tab's count unchanged.
- [x] **Step 2: Run them to see them fail.** Run `autoninja -C out\vanilla ghost_browsertests && out\vanilla\ghost_browsertests --gtest_filter=BlockedCount*`. Expected: the counts stay 0.
- [x] **Step 3: Patch 0037.** In `ChromeContentBrowserClient::WillCreateURLLoaderFactory`, change the call that 0029 added:

```cpp
  // First, so that blocked requests reach no other interceptor.
  ghost::blocking::MaybeProxyURLLoaderFactory(browser_context, frame,
                                              isolation_info, factory_builder);
```

  Commit it in `chromium/src` with `-s` and the trailers. `Why:` the panel counts a tab's blocked requests, and the frame tells which tab. `Upstream:` not upstreamable, product-specific.
- [x] **Step 4: The request filter.**
  - `MaybeProxyURLLoaderFactory` gains `content::RenderFrameHost* frame` after `context`.
  - `RequestFilter` gains `content::GlobalRenderFrameHostId frame` (default-constructed when `frame` is null) and keeps it as `const content::GlobalRenderFrameHostId frame_;`.
  - Where `InFlight` fails a request with `net::ERR_BLOCKED_BY_CLIENT` (`request_filter.cc:201`), first call `protections::PageProtections::RecordBlocked(frame_)`.
  - In `connection_filter.cc`, when the verdict blocks:
    - WebSocket: `RecordBlocked(frame ? frame->GetGlobalId() : content::GlobalRenderFrameHostId())`;
    - WebTransport: `RecordBlocked(content::GlobalRenderFrameHostId(process_id, frame_routing_id))`.
- [x] **Step 5: Run the tests.** Same command as Step 2, then the whole `ghost_browsertests` and `ghost_unittests`. Expected: all pass, no retry.
- [x] **Step 6: Commit.** In webops: `blocking: report each block against its frame (patch 0037)`. Re-export the series; `patches.py check`.

### Task 4: The panel's state and the level choice

**Files:**
- Create: `browser/protections/panel_state.{h,cc}` (deps `//ghost/browser/privacy_policy`, `//ghost/components/site`)
- Test: `browser/protections/panel_state_unittest.cc`

- [x] **Step 1: Write the failing tests** (`ChromeRenderViewHostTestHarness`):

```cpp
TEST_F(PanelStateTest, ASiteIsItsRegistrableDomain) {
  NavigateAndCommit(GURL("https://shop.example.org/cart"));
  PanelState state = GetPanelState(*web_contents());
  EXPECT_TRUE(state.applies);
  EXPECT_EQ(state.site, "example.org");
  EXPECT_EQ(state.level, ProtectionLevel::kStandard);
  EXPECT_EQ(state.mode_default, ProtectionLevel::kStandard);
  EXPECT_FALSE(state.off_the_record);
}

TEST_F(PanelStateTest, ProtectionsApplyToWebPagesOnly) {
  for (const char* url : {"chrome://settings/", "file:///C:/a.html", "chrome://newtab/",
                          "about:blank"}) {
    NavigateAndCommit(GURL(url));
    EXPECT_FALSE(GetPanelState(*web_contents()).applies) << url;
  }
}

TEST_F(PanelStateTest, ChoosingTheDefaultClearsTheSitesChoice) {
  NavigateAndCommit(GURL("https://news.test/"));
  ChooseLevel(*web_contents(), ProtectionLevel::kOff);
  EXPECT_TRUE(profile()->GetPrefs()->GetDict(privacy_policy::kSiteLevelsPref).contains("news.test"));
  ChooseLevel(*web_contents(), ProtectionLevel::kStandard);
  EXPECT_FALSE(profile()->GetPrefs()->GetDict(privacy_policy::kSiteLevelsPref).contains("news.test"));
}

TEST_F(PanelStateTest, ChoosingALevelReloads) {
  NavigateAndCommit(GURL("https://news.test/"));
  ChooseLevel(*web_contents(), ProtectionLevel::kStrict);
  EXPECT_EQ(controller().GetPendingEntry()->GetURL(), GURL("https://news.test/"));
  EXPECT_EQ(controller().GetPendingReloadType(), content::ReloadType::NORMAL);
}
```

  Also: an Incognito `WebContents` (`TestingProfile::GetPrimaryOTRProfile`) has `mode_default == kStrict` and `off_the_record`. If `GetPendingReloadType` doesn't exist at the pin, assert with `content::TestNavigationObserver` that a reload starts.
- [x] **Step 2: Run them to see them fail.** Expected: a compile failure.
- [x] **Step 3: Implement.**

```cpp
// What the protections panel shows for a tab's page.
struct PanelState {
  bool applies = false;       // an http(s) page
  std::string site;           // its registrable domain
  privacy_policy::ProtectionLevel level = privacy_policy::ProtectionLevel::kStandard;
  privacy_policy::ProtectionLevel mode_default = privacy_policy::ProtectionLevel::kStandard;
  int blocked_count = 0;
  bool off_the_record = false;
};

PanelState GetPanelState(content::WebContents& contents);

// Sets the page's site to |level|, or clears its choice when |level| is the
// mode's default, so a default changed later applies to it; then reloads.
void ChooseLevel(content::WebContents& contents, privacy_policy::ProtectionLevel level);
```

  The page is `contents.GetPrimaryMainFrame()->GetLastCommittedURL()`; `applies` is `SchemeIsHTTPOrHTTPS()`; the site is `ghost::RegistrableDomain(url.host_piece())`. The level and default come from `privacy_policy::GetLevel` and `ModeDefault`, and the count from `PageProtections` (0 without one). `ChooseLevel` calls `SetLevel` or `ClearLevel`, then `contents.GetController().Reload(content::ReloadType::NORMAL, /*check_for_repost=*/true)`. It does nothing when `!applies`.
- [x] **Step 4: Run the tests.** Expected: pass.
- [x] **Step 5: Commit.** `protections: the panel's state and the level choice`.

### Task 5: The Mojo interface and the page handler

**Files:**
- Modify: `browser/ui/protections/protections.mojom`, `protections_ui.{h,cc}`
- Create: `browser/ui/protections/protections_page_handler.{h,cc}`, `protections_strings.{h,cc}`

- [ ] **Step 1:** Write the full `protections.mojom`:

```
module ghost.protections.mojom;

enum Level { kOff, kStandard, kStrict };

struct State {
  bool applies;
  string site;
  Level level;
  Level mode_default;
  int32 blocked_count;
  bool off_the_record;
};

// Made by the page once it's loaded.
interface PageHandlerFactory {
  CreatePageHandler(pending_remote<Page> page, pending_receiver<PageHandler> handler);
};

// Browser side.
interface PageHandler {
  GetState() => (State state);
  SetLevel(Level level);
  // The page has rendered: the bubble can show.
  ShowUI();
};

// Page side.
interface Page {
  OnStateChanged(State state);
};
```

- [ ] **Step 2:** Write `ProtectionsPageHandler(mojo::PendingReceiver<PageHandler>, mojo::PendingRemote<Page>, content::WebUI*, ProtectionsUI*)`.
  - It finds the tab with `webui::GetBrowserWindowInterface(web_ui->GetWebContents())->GetTabStripModel()->GetActiveWebContents()` and observes its `PageProtections` (creating it).
  - On `OnBlockedCountChanged` and on the tab's `DidFinishNavigation`, it sends `page_->OnStateChanged(ToMojo(GetPanelState(*tab)))`.
  - `SetLevel` calls `ChooseLevel`; `ShowUI` calls `ui->embedder()->ShowUI()` when there is an embedder.
- [ ] **Step 3:** Fill `protections_strings.cc`: `kTitleBlocked` ("requests blocked on this page"), `kOffTitle` ("nothing is blocked on this site"), the three level names, the three sentences of the spec, `kDefault` ("default"), `kIncognitoNote`, `kNotApplicable` ("Protections don't apply to this page"), and the accessible-name format "Protections: $1, $2 requests blocked". `ProtectionsUI`'s constructor adds them to the `WebUIDataSource` with `AddString`.
- [ ] **Step 4:** Build. Expected: compiles; the spike's page still opens.
- [ ] **Step 5: Commit.** `protections: the page handler`.

### Task 6: The page

**Files:**
- Modify: `browser/resources/protections/app.ts`, `app.html.ts`, `browser_proxy.ts`, `protections.html`; add `app.css` (`css_files` in `BUILD.gn`)

- [ ] **Step 1:** `browser_proxy.ts`: a singleton that makes the `PageHandlerFactory` remote, creates a `PageCallbackRouter`, and exposes `handler` and `callbackRouter` (as personal-context-notice's `browser_proxy.ts` does).
- [ ] **Step 2:** `app.ts`: `ProtectionsAppElement` (LitElement, tag `protections-app`).
  - On `connectedCallback`: `await handler.getState()`, listen to `onStateChanged`, then `handler.showUI()` after the first render.
  - It renders the header (the site), and then:
    - if `level !== kOff`: the count and `kTitleBlocked`;
    - if `level === kOff`: "Off" and `kOffTitle`.
  - Below that:
    - a `cr-radio-group` (from `//ui/webui/resources/cr_elements`) with three `cr-radio-button`s, the default one labelled "Name · default";
    - the level's sentence;
    - the Incognito note when `offTheRecord`.
  - `selected-changed` calls `handler.setLevel(level)`.
- [ ] **Step 3:** Style `app.css` with the colour variables of `//ui/webui/resources/cr_elements/cr_shared_vars.css` only, no literal colours. Width 300 px; spacing as in the approved mockup (`.superpowers/brainstorm/.../panel-states.html`).
- [ ] **Step 4:** Build and open the bubble on a tracker page (`test/egress/site`). Expected:
  - the count rises while the page loads;
  - Off reloads the page and shows "Off";
  - Standard (the default) clears the choice: check `chrome://prefs-internals` for `ghost.privacy_policy.site_levels`.
- [ ] **Step 5: Commit.** `protections: the panel page`.

### Task 7: The button

**Files:**
- Modify: `browser/ui/protections/protections_button.{h,cc}`, `BUILD.gn`
- Create: `browser/ui/protections/vector_icons/BUILD.gn`, `shield_off.icon`

- [ ] **Step 1:** `shield_off.icon`: copy `components/vector_icons/shield.icon`'s path and append a slash:

```
NEW_PATH,
STROKE, 1.5f,
CAP_ROUND,
MOVE_TO, 3.5f, 3.5f,
LINE_TO, 16.5f, 16.5f
```

  Build it with `aggregate_vector_icons("protections_vector_icons")`, with `icon_directory = "."`, as `components/vector_icons/BUILD.gn` does.
- [ ] **Step 2:** `ProtectionsButton : ToolbarButton, TabStripModelObserver, PageProtections::Observer`.
  - It follows the active tab (`OnTabStripModelChanged` with `selection.active_tab_changed()`) and its navigations (a `content::WebContentsObserver` on the active tab).
  - On each change it calls `Update()`:
    - with `GetPanelState`: if `!applies`, the button is disabled with the tooltip `kNotApplicable`;
    - if the level is Off: `shield_off` in `kColorToolbarButtonIconInactive`, no badge;
    - otherwise: `kShieldIcon` in `kColorToolbarButtonIcon`, with an `IconWithBadgeImageSource` badge carrying the count (empty when 0, "99+" above 99), coloured `kColorToolbarButtonBackgroundHighlighted`.
    - Then `SetAccessibleName` with the format string.
  - Click: `bubble_manager_->ShowBubble(this)`. The manager is created in the constructor with `WebUIBubbleManager::Create<ProtectionsUI>(browser, GURL("chrome://protections.top-chrome"), <the string id settled in Task 1, Step 6>)`.
- [ ] **Step 3:** Build and check by hand: the badge appears on a tracker page, the shield turns grey and struck at Off, and the button is disabled on `chrome://settings`.
- [ ] **Step 4: Commit.** `protections: the toolbar button`.

### Task 8: Browser tests for the button and the panel

**Files:**
- Create: `browser/ui/protections/protections_panel_browsertest.cc` (add to `ghost_browsertests`)

- [ ] **Step 1:** Write the tests. Find the button by its element identifier: the button sets `SetProperty(views::kElementIdentifierKey, kProtectionsButtonElementId)` (declared with `DECLARE_ELEMENT_IDENTIFIER_VALUE` in `protections_button.h`, so no Chromium id list is patched), and the test gets it with `views::ElementTrackerViews::GetInstance()->GetFirstMatchingViewAs<ProtectionsButton>(kProtectionsButtonElementId, browser()->window()->GetElementContext())`. Cases:
  1. On a page with three tracker images, the badge text is "3" (`IconWithBadgeImageSource` isn't inspectable: expose `std::u16string badge_text_for_testing()`), and the accessible name is "Protections: Standard, 3 requests blocked".
  2. The bubble opens (`ShowBubble`, then wait for `WebUIBubbleManager::GetBubbleWidget()` to be visible). In its `WebContents`, `content::EvalJs(bubble, "document.querySelector('protections-app').shadowRoot.querySelector('.count').textContent")` is "3".
  3. Clicking Off in the page (`content::ExecJs`, clicking the Off radio) writes `site_levels["news.test"] == "off"`, reloads the tab (wait with `content::TestNavigationObserver`), makes the badge text empty, and puts the button in Off.
  4. On `chrome://settings` the button is disabled.
  5. In an Incognito browser (`CreateIncognitoBrowser()`), the bubble shows Strict as the default and the Incognito note.
- [ ] **Step 2:** Run them: `out\vanilla\ghost_browsertests --gtest_filter=ProtectionsPanel*`. Expected: pass. Then all of `ghost_browsertests` and `ghost_unittests` with no retry.
- [ ] **Step 3: Commit.** `protections: browser tests for the button and the panel`.

### Task 9: The look, with the user

- [ ] Run `out\vanilla\chrome.exe` with a fresh `--user-data-dir` (session scratchpad).
- [ ] The user checks the panel and the button on a tracker page and on an Off site: light and dark (`--force-dark-mode`), at 100% and 150% Windows scaling.
- [ ] Fix what the user asks for, rebuild, and show it again.
- [ ] Done when the user approves. Record their approval in the progress notes.

### Task 10: Mutation checks, audit, docs, push

- [ ] **Mutation checks.** Each must make at least one test fail; undo each with `sync.py`, then compare `src/ghost` with webops:
  - **M1:** `PrimaryPageChanged` doesn't reset.
  - **M2:** `RecordBlocked` counts for the active tab of the last active browser instead of the frame's tab.
  - **M3:** `ChooseLevel` always calls `SetLevel`.
  - **M4:** the WebSocket filter doesn't call `RecordBlocked`.
- [ ] **Full suites and the audit.**
  - Run `ghost_unittests` and `ghost_browsertests` in full.
  - Run the egress audit. Expected: 0 unexpected hosts; the preloaded page makes no request.
- [ ] **Docs.**
  - `privacy-model.md`: the panel; what the count counts; workers aren't in it.
  - `architecture.md`: UI as built; patches 0035–0037; the strings file.
  - `testing.md`.
  - `roadmap.md`: 3D-3 done.
  - The progress notes; the status of the spec and this plan.
- [ ] **Push.** Ask the user before pushing. After the push, wait for both tooling jobs; then update the memory.
