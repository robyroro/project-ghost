# The protections panel: progress notes

[Design](2026-10-10-protections-panel-design.md), [plan](../plans/2026-10-10-protections-panel.md).

## Task 1: an empty page end to end (spike), 2026-10-10

What the wiring took, beyond the plan:

- **Lit's build target has a visibility list.** `//third_party/lit/v3_0:build_ts` names each page allowed to use it; `gn gen` refused `//ghost/browser/resources/protections`. Patch 0036 adds `//ghost/browser/resources/*` to it.
- **A top-chrome page's name must be on an allow-list.** `WebUIContentsWrapperT` checks at compile time (`views_metrics::IsValidWebUIName`) that the controller's `GetWebUIName()` is a variant in `tools/metrics/histograms/metadata/page/histograms.xml`, generated into `webui_name_variants.h`. Patch 0036 adds `.Protections`. It names histograms only; Shade sends no metrics.
- **grit needs the page's resource ids.** `tools/gritsettings/resource_ids.spec` lists every grd with a start id; patch 0036 adds `ghost/browser/resources/protections/resources.grd` (20 ids from 10240, after `webui_toolbar_shared`).
- **No dependency cycle.** `//ghost/browser/ui/protections` depends on the toolbar's and the bubble's own targets (`//chrome/browser/ui/views/toolbar`, `//chrome/browser/ui/views/bubble`), not on `//chrome/browser/ui`; `//chrome/browser/ui/views/toolbar:impl` links it (patch 0035), `//chrome/browser:core` (the Mojo binder) and `//chrome/browser/ui/webui:configs` (the config) depend on it (patch 0036). `allow_circular_includes_from` wasn't needed, as the plan expected.
- **The task manager's name for the bubble** needs a string resource id, and Shade has none: upstream has no "Protections" string. The bubble uses `IDS_SETTINGS_PRIVACY` ("Privacy and security") until Shade's strings get a grd with translations. Known wording issue.
- **The pak** is `$root_gen_dir/ghost/protections_resources.pak` (`grit_output_dir = "$root_gen_dir/ghost"`, so the headers are `ghost/grit/protections_resources.h`), packed by `chrome/chrome_paks.gni` beside `tab_search_resources.pak`.
- **Workers' factories carry a frame.** `WillCreateURLLoaderFactory` gets the creator document for a dedicated worker's script and, with `kUseAncestorRenderFrameForWorker` (on by default at the pin), its ancestor frame for the worker's subresources (`dedicated_worker_host.cc`): blocks in dedicated workers count for their tab. Shared and service workers come without a frame.
- **Patch numbers follow commit order**: the spike's two patches came first, so the toolbar is 0035, the page's registration 0036, and the request filter's frame (Task 3) 0037.
- **Proof:** `ProtectionsPanelBrowserTest.TheButtonOpensThePanel` finds the button by its element identifier, clicks it, waits for the bubble (shown only after the page's `ShowUI` over Mojo, so the binder works), and reads "Protections" from `chrome://protections.top-chrome/`. Builds: 2m20s to the first error, then 6m32s (the allow-list header and the webui configs recompile); the test target 1m36s.

## Task 2: the count per tab, 2026-10-10

`PageProtections` as planned; 4 unit tests.

## Task 3: the filters report their blocks (patch 0037), 2026-10-10

- `RequestFilter` keeps its factory's frame and reports each block against it; the connection filter reports WebSocket and WebTransport blocks against theirs. Patch 0037 changes only the call 0029 added.
- `BlockedCountBrowserTest` (6 tests): images, a subframe, a dedicated worker's `fetch`, a WebSocket, a navigation's reset, and a background tab whose blocks don't reach the active tab. 84 unit and 72 browser tests pass, no retry.

## Task 4: the panel's state and the level choice, 2026-10-10

- `GetPanelState` and `ChooseLevel` as planned; 7 unit tests. Real WebUI pages (`chrome://settings`) don't load under `ChromeRenderViewHostTestHarness` (the controller's constructor crashes), so the tests use `chrome://no-such-page/` for a chrome: page; the browser tests (Task 8) use the real one.
- M3 (`ChooseLevel` writes the default instead of clearing it) checked now: `ChoosingTheDefaultClearsTheSitesChoice` fails. A first try with `if (false)` didn't compile (`-Wunreachable-code` under `/WX`) and the old binary ran green: a mutant's build result must be checked before its tests are read.

## Tasks 5 and 6: the page handler and the page, 2026-10-10

One commit: the page and its Mojo interface only work together.

- The handler follows the browser's active tab, chosen again at each `GetState` (the page asks on load and each time it becomes visible: `WebUIBubbleManager` keeps a preloaded page for the next opening, maybe on another tab). It observes the tab's `PageProtections`, whose reset on a new page also sends the state, and stops before the tab's user data goes (`WebContentsDestroyed`).
- `build_webui` runs WebUI's ESLint: an HTML template may hold no local variables or `if` (logic goes to the class), and a Lit lifecycle override must call `super`.
- The levels are native radio inputs styled as a segmented control: radio-group semantics and arrow keys come with them, without `cr_elements` (another dependency to allow).
- Colours come from `chrome://theme/colors.css` (`--color-sys-*`), loaded by the page; nothing is hard-coded.
