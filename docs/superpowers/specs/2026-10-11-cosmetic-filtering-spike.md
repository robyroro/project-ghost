# Cosmetic filtering: progress notes

[Design](2026-10-11-cosmetic-filtering-design.md), [plan](../plans/2026-10-11-cosmetic-filtering.md).

## Task 1: rules before parsing, a scanner in its world (spike, patches 0041 and 0042), 2026-10-12

The spike's rules were fixed: the throttle prepared `.shade-spike{display:none !important}` with scanning on for every navigation, and the host answered `.shade-spike-generic` for that class only. Six browser tests passed 18 of 18 runs (`--gtest_repeat=3`).

**What the wiring proved:**
- **The push arrives before parsing.** A message on the frame's channel-associated interface, sent at `ReadyToCommitNavigation`, reaches the agent before the document is parsed, as `subresource_filter.mojom` relies on. The test is an inline script right after an element its rule hides: the script sees `display: none` in 30 navigations of 30. The navigations alternate between two sites, which swaps renderer processes. No pull was needed.
- **A frame its parent writes into, and a `srcdoc` frame, take the parent's rules.** The first gets them from the agent's constructor (its initial empty document; no navigation), the second through the throttle's `WillCommitWithoutUrlLoader`.
- **A page whose CSP is `script-src 'none'` is still scanned.** The scanner runs through `ExecuteScriptInIsolatedWorld`, which the page's CSP doesn't govern. No world CSP was needed.
- **The page sees nothing.** `typeof shadeCosmeticFilters` is `"undefined"` in its world, and `document.styleSheets` doesn't list the user-origin sheet.

**Found, beyond the plan:**
- **Blink forbids scripts in `DidCreateDocumentElement`.** `FrameLoader::DispatchDocumentElementAvailable` holds a `ScriptForbiddenScope`. The scanner's script silently didn't run there, though its world's context was created.
  - Scripts may run in `RunScriptsAtDocumentElementAvailable`, which content passes to `ContentRendererClient::RunScriptsAtDocumentStart`. That is where extensions' `document_start` scripts run, still before anything is parsed.
  - So patch 0041 also calls `CosmeticFiltersAgent::RunScriptsAtDocumentStart` there, before the extensions' scripts, which may delete the frame. The style sheet is still inserted at `DidCreateDocumentElement`, which runs no script.
  - A frame's initial empty document has no document start: its scanner starts from a posted task, and its first pass sees what the parent wrote meanwhile.
- **API names at the pin:**
  - `blink::WebString::FromUtf8` (not `FromUTF8`);
  - `GURL::path()` returns a `std::string_view`;
  - `BrowserInterfaceBrokerProxy` is in `third_party/blink/public/platform/`.
- **The Java mirror of the world ids** (`ChromeIsolatedWorldIds.java`) hard-codes each value. Shade's id is inserted after `CHROME_INTERNAL`, before the macOS-only entry, so the Java file gains one value and its two unused ones move up by one.
- **Startup traces from `--trace-startup` are Perfetto protobuf** unless `--trace-startup-format=json` is given.
  - In the dev build, the scanner's work is attributed to `shade://cosmetic-filters/scanner.js`: `EvaluateScript` for its start (with `v8.compile` and `v8.run` inside), and `FunctionCall` for each `MutationObserver` and timer callback.
  - The official build's startup trace had no events from the page's renderer, so Task 10 measures with DevTools' `Performance.getMetrics` instead.

**The scanner's cost, first numbers.** The fixture is `test/cosmetic/heavy.html`: 5,000 elements inserted at once, with 1,007 class names and 5,000 ids.
- **Dev build, scanner in its isolated world** (trace, three runs):
  - start: 4.4–7.1 ms;
  - `MutationObserver` callbacks: 95–125 ms, almost all of it in one callback, about 14 ms of which is minor GC.
- **What the time is spent on** (dev build, variants chosen by the URL's hash, two runs each):
  - **The calls to the browser cost nothing measurable:** no `report()` calls, 95–111 ms.
  - **Merely iterating the added nodes:** 21–25 ms.
  - **The rest is the DOM bindings' calls** for each element in the isolated world. Reading attributes instead of `classList`, and skipping `querySelectorAll` for elements without children, didn't change it measurably (kept, as it avoids a wrapper per element).
- **Dev against official** (`Performance.getMetrics`, five loads each):

  | Build | Page's own script | With a main-world copy of the scanner |
  |---|---|---|
  | dev (`out\vanilla`) | 172 ms | 217 ms |
  | official (`out\release`) | 9.1 ms | 20.7 ms |

  The dev build runs the same page script about 19 times slower. Dividing the dev build's 100 ms by that ratio puts the scanner near the budget of 5 ms for this page in an official build. Task 10 measures it there.
- **The rejected option, every generic rule in one style sheet** (13,199 selectors, 509 KiB of CSS, inlined in the same page): in the official build, the page's main-thread task time went from 46 ms to 76 ms, and style recalculation from 6.0 to 10.3 ms. That cost recurs in every document, ad frames included. The scanner costs only where there are elements to scan.

**Build:** the first build after the patches took 1m28s to the first error; each rebuild of `chrome` and `ghost_browsertests` took 42s–2m07s.

**Tools** (session scratchpad, not committed):
- `build.ps1`;
- `bt.ps1`: browser tests on the hidden desktop; it sets the process's current directory, which `Set-Location` doesn't;
- `trace.ps1`;
- `sum_trace.py`;
- `metrics.ps1` and `metrics.py`: DevTools metrics of a browser on the hidden desktop, stopping only that profile's processes.
