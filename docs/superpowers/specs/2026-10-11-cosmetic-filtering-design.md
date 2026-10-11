# Cosmetic filtering: design

- Status: design approved 2026-10-11.
- Phase 4, sub-project 4A ([roadmap](../../roadmap.md#phases-to-public-alpha)); builds on [3A, the blocking engine](2026-10-10-blocking-engine-design.md) and [3D-1, the protection levels](2026-10-10-protection-levels-design.md)
- Phase 4 is split (decided 2026-10-11): **4A** cosmetic filtering (element hiding); **4B** scriptlets and procedural rules (one in-page script layer for both); **4C** CNAME uncloaking; **4D** breakage controls and the compatibility corpus.

## Goal

Elements the lists mark as ads are hidden in every frame of a page: the site-specific rules before the page is parsed, the generic rules as the classes and ids they name appear. The page can't see or override the hiding, and the renderer, which isn't trusted, can't ask for more than the rules for its own document.

## Decisions

- **The levels** (the user's choice, 2026-10-11): *Off* hides nothing. *Standard* and *Strict* hide the same: the site-specific rules and the generic ones, as uBlock Origin does by default. EasyList is maintained to be applied whole; a site it breaks gets Off from the panel. privacy-model.md's "Strict applies cosmetic rules more aggressively" is corrected to say what is built; a stronger Strict waits for a list of our own (annoyances, cookie banners).
- **Brave's model, with a scan** (the user's choice among three, 2026-10-11), which is what adblock-rust's cosmetic API is built for:
  - `url_cosmetic_resources(url)` gives a document's site-specific selectors, the generic selectors too complex to index by class or id (`div[class*="Ads"]`), the exceptions (`#@#`) and `generichide`;
  - `hidden_class_id_selectors(classes, ids, exceptions)` gives the generic selectors that match the classes and ids a page actually has.
  - A document gets the few dozen selectors that concern it, not EasyList's 13,600 generic ones. Rejected: one stylesheet with every generic rule in every frame (each document, every ad iframe included, parses about 400 KB of CSS; Brave and uBlock Origin both left it for speed; the spike measures it for the record), and a scan in C++ through Blink hooks on `class` and `id` (deep Blink patches to carry through every rebase).
- **The renderer isn't trusted.** The browser keeps each document's exceptions and its level; the renderer sends only class and id names and gets selectors back. It can't ask for another site's rules or get around an exception.
- **The page's level is its outermost main frame's**, as for request blocking. Each frame gets the rules for its own document's URL; `about:blank` and `srcdoc` frames use the origin that created them.
- **Hiding is a user-origin stylesheet** (`WebDocument::InsertStyleSheet` with `kUser`), one rule per selector: `selector { display: none !important; }`. A user `!important` declaration wins over the page's, and the sheet isn't in `document.styleSheets`. One rule per selector because in a selector list one invalid selector drops the whole rule.
- **Selectors containing `{`, `}`, `;`, `@`, `/*` or `*/` are dropped** before they reach CSS: lists will arrive by update (3E), and a selector that closes its rule would inject arbitrary style.
- **The scanner is our own small JavaScript in an isolated world of its own**: a world id added to `chrome/common/chrome_isolated_world_ids.h` by a patch, before the extensions' range. It sees the DOM, not the page's scripts, and the page can't see it. It reaches C++ through a gin object installed only in that world. The script is compiled into the binary by `tools/embed_text.py` (as 3B's list), so no grd or pak.
- **Not in 4A:** scriptlets (EasyList has none) and EasyList's 295 procedural rules (`:has-text`, `:-abp-has`) go to 4B, which builds the in-page script layer both need. The engine parses every rule type now; the rules it can't act on are ignored. Hidden elements aren't counted on the shield: its number stays requests blocked, which the browser knows for certain.
- **Exceptions honored:** `#@#`, `$generichide` and `$elemhide` (adblock-rust folds `$elemhide` into the same answers).

## Components

| Unit | What it does |
|---|---|
| `components/blocking/rust/lib.rs` | Two bridge functions: `cosmetic_resources(url) -> CosmeticResources { hide_selectors: Vec<String>, exceptions: Vec<String>, generichide: bool }` and `generic_selectors(classes: &[String], ids: &[String], exceptions: &[String]) -> Vec<String>`. The engine is built with `RuleTypes::All`. |
| `BlockingEngine` | `GetCosmeticRules(GURL, reply)` and `GetGenericSelectors(classes, ids, exceptions, reply)`, on the engine's sequence; like `Check`, they wait while the engine loads, and a failed engine answers empty. |
| `//ghost/components/cosmetic_filters` | No `//content`: `StyleSheetFor(selectors)` (drops unsafe selectors, one rule each); `ValidateNames(classes, ids)` (at most 1,000 names in a call, each at most 256 bytes, UTF-8, non-empty). |
| `//ghost/browser/cosmetic_filters` | `CosmeticFiltersHost`, a `content::DocumentService` per document implementing `ghost.mojom.CosmeticFiltersHost`. Resolves the page's level (3D-1's `GetLevel` on the outermost main frame's URL); keeps the document's exceptions and `generichide`; answers `GetGenericSelectors(classes, ids) => (selectors)`; a call that fails `ValidateNames` is reported with `ReportBadMessage`. |
| `//ghost/renderer/cosmetic_filters` | `CosmeticFiltersAgent`, a `content::RenderFrameObserver`: receives the document's rules, inserts them at `DidCreateDocumentElement`, starts the scanner unless the level is Off or `generichide` is set, forwards its batches to the host and inserts the selectors returned. |
| `//ghost/renderer/cosmetic_filters/resources/scanner.js` | Puts a `MutationObserver` on the document at its start; collects class and id names into a set; sends only names not sent before: the first batch at once, then at most one batch every 100 ms. |
| `//ghost/common/cosmetic_filters.mojom` | `CosmeticFiltersHost` (renderer to browser); `CosmeticFiltersAgent` (browser to renderer: `SetRules(string style_sheet, bool scan)`), channel-associated if the spike proves the ordering (below). |
| Patches | The agent created in `ChromeContentRendererClient::RenderFrameCreated`; the host bound in the frame binders (as patch 0036); Shade's isolated world id. Numbers follow commit order. |

## Data flow

1. **The site-specific rules (push).** At `ReadyToCommitNavigation`, `CosmeticFiltersHost` asks the engine for the document's rules and sends the stylesheet to the frame's agent over a channel-associated interface; the agent inserts it at `DidCreateDocumentElement`, before any element is parsed. If no rules have arrived by then, the agent inserts them when they do.
2. **The generic rules (scan).** The scanner starts at the document's start, so it sees elements as they're parsed. Each batch goes to the host, which answers with the matching selectors, kept out of the exceptions; the agent adds them to the same stylesheet.
3. **The levels.** Off: no stylesheet, no scanner. A change of level takes effect at the next load; the panel already reloads the page.

An element added by script can show until its batch's answer arrives, as in Brave and uBlock Origin; the documentation says so.

## For the spike

1. **The order of push and commit.** Whether a message on a channel-associated interface sent at `ReadyToCommitNavigation` reaches the agent before `DidCreateDocumentElement`, given that `CommitNavigation` travels on `NavigationClient`, a separate pipe. If not guaranteed: the agent pulls the rules at the document's start, and the spike measures what can paint before the answer; the acceptance test below decides.
2. **The isolated world and the gin object.** Creating the world at `DidCreateDocumentElement` (or `DidCreateScriptContext` for the main world), running the scanner there before the page's scripts, and that the page's world sees neither the object nor the observer.
3. **The measurements:** the engine's memory and compile time with every rule type (now about 6 MiB and 80 ms); and the rejected option, every generic rule as one stylesheet, on the heavy fixture page, for the record.

## Errors

- The engine loading: the rules wait for it, so the first pages after startup are filtered. The engine failed: no rules, nothing hidden, a log line.
- The isolated world can't be created: the site-specific rules stay, a log line.
- A renderer that sends names failing `ValidateNames`: `ReportBadMessage`, which ends the renderer.

## Performance budgets

Measured in the official build, reported; changed only with the user's agreement.

| What | Budget |
|---|---|
| A document's rules, on the engine's sequence (`ghost_blocking_perftests`) | p99 under 1 ms |
| A batch of 1,000 class and id names | p99 under 1 ms |
| The scanner's first pass over a fixture page with 5,000 elements, on the renderer's main thread | under 5 ms |
| The engine's memory and compile time with cosmetic rules | reported against 6 MiB and 80 ms; no budget |

## Testing

**Unit tests** (`ghost_unittests`):
- the bridge: site-specific selectors for a host, exceptions, `generichide`, `$elemhide`, generic selectors for classes and ids, an exception removing a generic one;
- `BlockingEngine`'s two methods, while loading and after a failed load;
- `StyleSheetFor` (unsafe selectors dropped, one rule each) and `ValidateNames` (limits, UTF-8, empty names);
- a fuzzer for the cosmetic queries, run 30 minutes.

**Browser tests** (`ghost_browsertests`, `CosmeticFiltersBrowserTest`) with a test list:
- a site-specific rule: the element is already hidden for an inline script right after it;
- a generic rule: an element with a listed class, added by script after load, is hidden;
- `#@#` and `$generichide` exceptions: the elements show;
- Off: nothing hidden; Incognito (Strict): hidden;
- a cross-site iframe: filtered by its own document's rules;
- the page sees neither the gin object nor our stylesheet (`document.styleSheets`), and its own `!important` style doesn't show a hidden element;
- a renderer sending 1,001 names is ended as a bad message.

**Mutation checks**, each must fail: M1 exceptions ignored; M2 the Off level ignored; M3 no generic rules; M4 no limits on names; M5 unsafe selectors kept.

**The egress audit** stays at no unexpected host.

## Documentation

privacy-model.md (what each level hides; Strict corrected), architecture.md (the renderer side, the patches), threat-model.md (a new renderer-to-browser interface and its limits), testing.md, roadmap.md (Phase 4 split into 4A–4D; 4A done), progress notes.

## Done when

- [ ] The tests pass with no retry and the mutation checks fail as required.
- [ ] The budgets hold in the official build, or the user agreed to new ones.
- [ ] The egress audit finds no unexpected host.
- [ ] The documentation is updated; everything is pushed with tooling CI green.

## Out of scope

- Scriptlets and procedural rules (4B), CNAME uncloaking (4C), breakage controls and the compatibility corpus (4D).
- Lists beyond EasyList and EasyPrivacy, and list updates between releases (3E).
- Counting hidden elements in the panel.
