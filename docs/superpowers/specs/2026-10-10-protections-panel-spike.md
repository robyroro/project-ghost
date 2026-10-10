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
