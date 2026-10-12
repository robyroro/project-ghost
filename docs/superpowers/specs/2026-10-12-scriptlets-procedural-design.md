# Scriptlets and procedural rules: design

- Status: done 2026-10-12 ([progress notes](2026-10-12-scriptlets-procedural-spike.md)); budgets measured in the official build, the scriptlets' one amended with the user, 4A's scanner optimized to meet its own. Design approved 2026-10-12; amended while planning ([plan](../plans/2026-10-12-scriptlets-procedural.md)): CSS validation turned on in adblock-rust, the operators 0.13 has, redirects as `data:` URLs, scriptlets wrapped in one function, pure-CSS action rules in the style sheet.
- Phase 4, sub-project 4B ([roadmap](../../roadmap.md#phase-4-content-protections)); builds on [4A, cosmetic filtering](2026-10-11-cosmetic-filtering-design.md), [3A, the blocking engine](2026-10-10-blocking-engine-design.md) and [3D-1, the protection levels](2026-10-10-protection-levels-design.md).

## Goal

The scriptlets (`##+js(...)`), procedural rules (`#?#…:has-text(…)`) and redirect rules (`$redirect=`) in the lists Shade ships take effect: scriptlets in the page's own world before any of its scripts run, procedural rules from an isolated world the page can't see, redirects answered by the browser with a resource of our own. The renderer still gets only what concerns its own document, and nothing a list says becomes code except through the functions we wrote.

## Decisions

- **The lists** (the user's choice, 2026-10-12): EasyList and EasyPrivacy only, the lists we ship. In them:
  - 294 procedural rules in EasyList: 286 `:has-text`, 8 `:-abp-properties` (adblock-rust 0.13 refuses `:-abp-properties`; those 8 are skipped);
  - 28 scriptlet rules in EasyPrivacy, using `set`, `acs`, `aost`, `rmnt`, `no-xhr-if`, `no-fetch-if`, `set-local-storage-item`, `set-cookie`, `cookie-remover` and `json-edit`;
  - 28 redirect rules, to `noopjs`/`noop.js`, `noop.txt`/`nooptext`, `noopjson`, `1x1.gif`, `chartbeat.js`, `googletagmanager_gtm.js` and `fingerprint2.js`.

  uBlock Origin's lists (GPL-3.0, legal review pending, 100+ scriptlets) and AdGuard's (GPL-3.0, their own `#%#//scriptlet` syntax) are not part of 4B: adding lists is a question of licenses and of 3E's list delivery. The layer built here takes them without change.
- **GPC** (the user's choice, 2026-10-12): EasyPrivacy sets `navigator.globalPrivacyControl` to `false` on about 55 sites whose scripts break when it is `true`. We apply those rules as the list says. The `Sec-GPC: 1` header, the signal with legal weight, is still sent on every request: the network stack sends it, not the page. privacy-model.md says so.
- **The levels:** as in 4A. *Off* applies nothing; *Standard* and *Strict* apply the same scriptlets, procedural rules and redirects.
- **One pipeline, 4A's** (the user's choice among three, 2026-10-12): the engine gives a document's scriptlet and procedural rules with its other cosmetic rules; the navigation throttle pushes them before the document is parsed. Rejected: the renderer pulling its scriptlets at the document's start (needs a synchronous IPC or arrives after the page's first scripts), and a bundled component extension in the manner of uBlock Origin Lite (`userScripts`, `declarativeNetRequest`: the extension system on the critical path, MV3's limits, a second infrastructure beside 4A's).
- **Our own resources, written clean.** uBlock Origin's scriptlet and redirect resources are GPL-3.0 ([licensing.md](../../licensing.md#filter-lists-and-scriptlets)). We write ours under MPL-2.0 from the behavior uBlock Origin's wiki describes publicly, under the names and argument orders the lists use, without reading uBlock Origin's `scriptlets.js`. The surrogates (`chartbeat.js`, `googletagmanager_gtm.js`, `fingerprint2.js`) are minimal stubs of the APIs pages call.
- **Function-style resources only.** adblock-rust 0.13 calls a function-style resource as `name(arg1, arg2, …)` with each argument a JSON string literal, so a list's argument can't become code. A template-style resource (`{{1}}`) pastes the argument into the code; none is accepted.
- **Scriptlets run in the main world**, because they change what the page's scripts see. They take references to the natives they rely on (`Object.defineProperty`, `Reflect`, `RegExp`, `JSON`, …) from a shared dependency that runs first, before any page script can replace them.
- **Procedural rules run in our isolated world**, beside the scanner. The executor is our own JavaScript; brave-core's `procedural_filters.ts` (MPL-2.0) was considered and not taken: it is TypeScript built by Brave's build, and ours stays plain JavaScript embedded like `scanner.js`.
- **CSS validation on** (*amended while planning*). 3A built adblock-rust without its `css-validation` feature, which deferred CSS to Phase 4. Without it a procedural rule is never parsed into operators: it arrives as one invalid selector that Blink drops. 4B turns the feature on, which vendors `cssparser`, `selectors` and their dependencies with `tools/rust_vendor.py`; it also makes the engine refuse invalid selectors and `:style()` values with `url(`, `image-set(`, `\` or `/*` when it parses the lists.
- **A procedural operator inside `:has()`** (*amended while executing*): 63 EasyList rules write `a:has(p:has-text(Ad))`. adblock-rust passes `:has()` through as CSS, so the bridge moves such a hide selector to the procedural rules as one `css-selector`, and the executor parses it: `:has()` with a procedural selector inside keeps the elements that have a descendant (or, with a leading combinator, a relative) matching it.
- **Not in 4B:** `json-edit` (one rule, on one site; it needs uBlock Origin's whole JSONPath dialect). The engine skips a scriptlet it has no resource for; the documentation lists it as a limitation.

## Components

| Unit | What it does |
|---|---|
| `components/blocking/resources/` (new) | Our resources, MPL-2.0: the JavaScript ones one file each, all listed in `components/blocking/rust/resources.rs` (name, aliases, MIME type, dependencies; the non-JavaScript bodies are bytes there). **Scriptlets** (`fn/javascript`): `set-constant` (`set`), `abort-current-script` (`acs`), `abort-on-stack-trace` (`aost`), `remove-node-text` (`rmnt`), `no-xhr-if`, `no-fetch-if`, `set-local-storage-item`, `set-cookie`, `cookie-remover`, and the shared dependency holding the natives. **Redirects:** `noop.js` (`noopjs`), `noop.txt` (`nooptext`), `noop.json` (`noopjson`), `1x1.gif` (`1x1-transparent.gif`), `chartbeat.js`, `googletagmanager_gtm.js`, `fingerprint2.js`. |
| `components/blocking/rust/lib.rs`, `resources.rs` | Builds the resources into the binary (`include_bytes!`) and gives them to the engine (`use_resources`). `CosmeticResources` gains `injected_script: String` and `procedural_actions: Vec<String>` (adblock-rust's JSON, one rule each, sorted). `Verdict` gains `redirect: String`: when a redirect rule blocked the request, the resource as a `data:` URL (adblock-rust builds it). Action rules expressible in CSS (a plain selector with `:style()`) come back as `style_rules { selector, style }`; only the others stay JSON. |
| `BlockingEngine` | Passes the new fields through. |
| `//ghost/components/cosmetic_filters` | `IsSafeStyle(declarations)`: the `:style()` action's declarations can't leave their rule: no `{`, `}`, `@`, `/*`, `*/`, `\`, `url(` or `image-set(`, no newline (`;` separates declarations and is allowed). `StyleSheetFor` gains the style rules, each checked; a procedural rule whose style fails is dropped (`SafeProceduralFilters`, which parses the JSON in the browser) before it leaves the browser. |
| `CosmeticFiltersThrottle`, `PreparedRules` | Carry the document's scriptlet and procedural rules with its style sheet. |
| `//ghost/common/cosmetic_filters.mojom` | `SetRulesForNextDocument(DocumentRules rules)`, with `struct DocumentRules { string style_sheet; bool scan; string scriptlet; array<string> procedural_filters; }`. |
| `CosmeticFiltersAgent` | At the document's start (`RunScriptsAtDocumentStart`), in this order: runs the scriptlet in the main world (`WebLocalFrame::ExecuteScript`), wrapped in one function so no name is left on `window`, starts the scanner, and starts the procedural executor when the document has procedural rules. A document no navigation brought takes its local parent's rules, scriptlet included (as 4A). |
| `//ghost/renderer/cosmetic_filters/procedural.js` (new) | In the isolated world. Evaluates every operator adblock-rust 0.13 can emit: `css-selector`, `has-text`, `matches-attr`, `matches-css`, `matches-css-before`, `matches-css-after`, `matches-path`, `min-text-length`, `upward`, `xpath`; and every action: hide (the default), `style`, `remove`, `remove-attr`, `remove-class`. Runs once at the start, then on DOM mutations at most once every 100 ms. An operator it doesn't know, or a rule that throws, skips that rule only. Hides an element by marking it with an attribute whose name is chosen at random per document, matched by a user-origin style sheet rule, so the page's own style can't show it again; the page can see the attribute, and if it removes it the next pass marks the element again. |
| `RequestFilter` | On a verdict with a redirect, answers the request itself: status 200, the resource's MIME type and body, and the headers the request's mode needs (the spike settles CORS). The request still counts as blocked on the shield. A redirect to a resource we don't have blocks, as today. |
| Patches | None expected: 0029, 0041 and 0042 have the hooks. The spike confirms. |

## Data flow

1. **The rules (push).** At `WillProcessResponse` (or `WillCommitWithoutUrlLoader`) the throttle asks the engine for the document's rules, now with its scriptlet and procedural rules, and defers until the answer, as in 4A. Off: no rules. At `ReadyToCommitNavigation` the tab helper sends `DocumentRules` over the channel-associated interface; 4A's spike showed it arrives before the document is parsed.
2. **In the renderer.** `DidCreateDocumentElement` inserts the style sheet, as in 4A. `RunScriptsAtDocumentStart`, the point where extensions' `document_start` scripts run and before any of the page's `<script>`, runs the scriptlet in the main world, then starts the scanner and the procedural executor in the isolated world.
3. **Frames.** Each frame gets the rules for its own document's URL; `about:blank` and `srcdoc` frames take their parent's. Whether the start hook fires for a frame's initial empty document that a parent's script writes into is for the spike; if it doesn't, the scriptlet doesn't run there, and the documentation says so.
4. **Redirects.** The request filter asks the engine as today; a verdict with a redirect carries the resource as a `data:` URL, which the filter decodes and answers with, without the network.

## Security

- **The scriptlet comes only from the browser.** The renderer can't ask for scriptlets or choose them; it gets the ones for its own document, as for the style sheet.
- **A list's arguments can't become code**: every resource is function-style, and adblock-rust passes the arguments as JSON string literals. A unit test fails on any template-style resource.
- **The page sees the scriptlets, by design.** They run in its world to change what its scripts see. They take their natives before any page script runs, so the page can't replace a native to defeat them.
- **The procedural executor is out of the page's reach.** It runs in the isolated world: the page can't see it or change the prototypes it uses. It reads text and styles from the page, which it never runs.
- **Regular expressions in rules** (`has-text(/…/)`) come only from the lists we ship (in the installer now, signed from 3E). The list is the trust boundary, as for network rules; threat-model.md records the risk of a slow regular expression without a separate time limit.
- **`:style()` declarations are checked in the browser** before they reach the renderer.
- **Redirect bodies are fixed**, built into the binary, with the resource's MIME type.

## For the spike

1. The order: the scriptlet runs before a page's first inline script, and before a parser-inserted external one, at `RunScriptsAtDocumentStart`; a page whose CSP forbids inline scripts still gets it (`ExecuteScript` isn't subject to the page's CSP).
2. Frames: which hooks fire for a frame without `src`, a `srcdoc` frame and a frame a parent writes into; whether the scriptlet can run in the last.
3. Redirects: the response type and headers a `fetch` in `cors` mode needs to accept a body the request filter supplies itself (it doesn't pass the network service's `CorsURLLoader`); a script and an image as well.
4. With CSS validation on: how many of the lists' 294 procedural rules parse, into which operators (`:has(… :has-text(…))` nests a procedural operator inside a native one), and how many plain selectors the validation now refuses.
5. The engine's memory and compile time with CSS validation and the resources.

## Errors

- The engine failed: no rules, nothing applied, a log line (as 4A).
- A scriptlet throws: adblock-rust wraps each call in `try/catch`; the others still run.
- A scriptlet with no resource (`json-edit`): the engine skips it.
- An unknown procedural operator, or a rule that throws: that rule is skipped.
- The isolated world can't be created: the style sheet and the scriptlet stay, no procedural rules, a log line.

## Performance budgets

Measured in the official build and reported; changed only with the user's agreement.

| What | Budget |
|---|---|
| A document's rules, with the scriptlet and procedural rules (`ghost_blocking_perftests`) | p99 under 1 ms |
| The scriptlets alone on the lists' site with the most scriptlets: what cosmetic filtering adds to `light.html` there, less what it adds at cosmetic.test (`tools/scanner_cost.py --site`) | under 1.5 ms (*amended with the user's agreement, 2026-10-12*: the first form, everything cosmetic filtering adds to the page under 1 ms, couldn't hold, since 4A alone takes 1.0 ms there) |
| What procedural rules add to the 5,000-element fixture page on the lists' site with the most procedural rules (that site's cost less `cosmetic.test`'s) | under 5 ms |
| The engine's memory and compile time with the resources | reported against 4A's; no budget |

## Testing

**Unit tests** (`ghost_unittests`):
- the bridge: `injected_script` for a site with `+js`, the procedural JSON, a redirect verdict, the aliases (`acs`, `set`, `noopjs`, …), the resource body by name;
- every scriptlet is function-style, no resource has a template placeholder, and every dependency exists;
- `IsSafeStyle`;
- `BlockingEngine`'s new fields while loading and after a failed load.

**Browser tests** (`ghost_browsertests`) with a test list:
- each scriptlet has its effect, already for the page's first inline script, and on a page whose CSP forbids inline scripts;
- each procedural operator and each action, on an element present at load and on one added later by script; the page can't see the executor;
- the `#@#+js()` exception; Off; Incognito (Strict);
- redirects: a script gets `noop.js`, a `fetch` in `cors` mode gets `noop.json`, an image gets `1x1.gif`;
- GPC: on a site in the test list, `navigator.globalPrivacyControl` is `false` and the request still carries `Sec-GPC: 1`.

**A fuzzer** for `IsSafeStyle` and `SafeProceduralFilters` (`ghost_cosmetic_procedural_fuzzer`; the cosmetic-query fuzzer's input can't reach them, since the lists are fixed), run 30 minutes.

**Mutation checks**, each must fail:
- M1 the scriptlet runs after the page's scripts;
- M2 a template-style resource is accepted;
- M3 the Off level ignored for scriptlets;
- M4 `:style()` unchecked;
- M5 a redirect becomes a plain block;
- M6 the procedural executor runs in the main world.

**The egress audit** stays at no unexpected host.

## Documentation

privacy-model.md (what the levels apply; the GPC rules and the header), architecture.md (the main-world scriptlet, the executor, redirects), threat-model.md (main-world code, list regular expressions), licensing.md (our resources, written clean), testing.md, roadmap.md (4B done), progress notes; `components/blocking/resources/README.md`.

## Done when

- [x] The tests pass with no retry and the mutation checks fail as required.
- [x] The budgets hold in the official build.
- [x] The egress audit finds no unexpected host.
- [x] The documentation is updated; everything is pushed with tooling CI green.

## Out of scope

- Lists beyond EasyList and EasyPrivacy, and their scriptlets (uBlock Origin's, AdGuard's); list updates between releases (3E).
- `json-edit` and uBlock Origin's other scriptlets the shipped lists don't use.
- CNAME uncloaking (4C), breakage controls and the compatibility corpus (4D).
