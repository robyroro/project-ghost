# The compatibility corpus: progress notes

Phase 4D-1 ([design](2026-10-15-compatibility-corpus-design.md), [plan](../plans/2026-10-15-compatibility-corpus.md)).

## Tasks 1–5: the harness (2026-10-15)

- `compat_corpus`, `compat_verdict`, `compat_page` and `compat`: 45 tests pass, written before each module.
- The self-test's pages: `blocked.html` needs `/adobe-analytics.js` (EasyPrivacy, line 182, no option) from `tracker.compat-other.test`; `fine.html` needs `/ready.js`, which no rule names.

## Task 6: the baseline and the first self-test (2026-10-15)

- **Chrome for Testing 152.0.7977.167** (`win64/chrome-win64.zip`, 203,007,504 bytes), fetched with the user's approval: MD5 `PYUXWGdxnWPydUNE9bSw4g==`, as Google's `x-goog-hash` gives it; SHA-256 `16442aeb519548a3cff25a6b7c521788193c2d1be85745a7904fbcaf15a13793`, recorded in `test/compat/baseline.json`.
- **The self-test** against the official build (`out\release`, 4C's), on the hidden desktop, passed at the first run:
  - `blocked.html` broken: failed step 1, `#ready` never visible; the baseline had it;
  - `fine.html` passed;
  - no browser or profile left behind.

## Task 7: Shade's own list (2026-10-15)

- `shade.txt` in `components/blocking/`, read last; the bridge's `list_problems`; patch 0030 packs it (one line in `chrome.release`, the message updated), squashed into its commit with an `amend!` commit and `--autosquash`; `patches.py export` changed only 0030.
- `EngineBridgeTest.ListProblemsNamesTheLinesTheParserRefuses`, `ShadesListHasNoProblem` and `FilterListsTest.ShadesOwnListIsReadLast` pass (development build, 12 minutes after the rebase).
- They fail as required:
  - **S3**, `||b.test^$nosuchoption` added to `shade.txt`: `line 12: … network filter error: unrecognised option`;
  - **S1**, `shade.txt` left out of `kFilterListFiles`: `ShadesOwnListIsReadLast`.

## Task 8: the corpus (2026-10-15)

- 155 sites in ten categories, 22 flows.
- Each flow was checked against Chrome for Testing alone. 15 passed at once; the other seven were fixed in the built-in browser:
  - DuckDuckGo's box is a zero-width textarea until focused: the flow starts from `?q=privacy+browser`;
  - OpenStreetMap's search box isn't shown at the window's width: `/search?query=Cluj-Napoca`, then its sidebar's text;
  - MDN's search results aren't plain elements: the Fetch API page and its heading;
  - Etsy shows `[data-listing-id]` cards from `/search?q=mug`, after a DataDome check that reloads the page;
  - Google Maps' box is `input[name=q]` now, after its EU consent page;
  - Booking's autocomplete never showed (a bot challenge, `chal_t`): the flow checks the search field only;
  - CFR's first `form` is hidden: its station field.
- Consent banners are refused (`#W0wltc`, `Reject`, `#onetrust-reject-all-handler`), never accepted; eMAG showed none to a fresh profile.
- The second check: 22 of 22 passed.

## Task 9: the first full run and its triage (2026-10-15)

- **The run:** the official build with `shade.txt` (15 minutes to build after the rebase) against Chrome for Testing, Standard then Strict, on the hidden desktop: 48 minutes in all.
  - Standard: 125 of 151 passed (82.8%), 26 broken, 1 flaky, 4 skipped (the baseline got 401, 403, 403, 429).
  - Strict: 118 of 149 (79.2%), 31 broken.
- **Triage** used a session tool that loads one site in Shade with the request filter's block log (`--vmodule=request_filter=1`, `--log-file`) and prints every exception with its stack and execution context. The log can't name the rule: the engine keeps no debug information.
- **Two Shade bugs**, fixed with tests:
  - **Cosmetic filtering in same-origin frames** (walmart.com, aliexpress.com, newegg.com, costco.com): `procedural.js` threw `reading 'forEach'` in Shade's world. Blink keeps a frame's window, and its isolated context, for the first document after the initial empty one when both are same-origin, so the api stayed bound to the empty document, whose weak pointers the new one had invalidated; the scanner was silent there too. Each document now binds the api again before its scripts start. `ProceduralBrowserTest.ASameOriginFrameAfterItsEmptyDocumentApplies` failed before (`block`) and passes. The first form crashed renderers: V8 wants a microtasks scope around API calls outside script.
  - **`$document` exceptions were ignored.** adblock-rust applies them only to the document request, which Shade never checks. The bridge now keeps the lists' `$document` exceptions in an engine of their own and asks it about the page; the main engine would also apply a typeless exception to a document, which the first try did (`AnExceptionWithoutDocumentLeavesThePageBlocked` caught it).
- **Two `shade.txt` fixes:**
  - **paypal.com/signin answered 403**, "Please enable JS and disable any ad blocker": its DataDome check (`ct.ddc.paypal.com`, then `geo.ddc.paypal.com`) is CNAMEd to `*.fpc.datadome.co`, which `||datadome.co^$third-party` blocks once uncloaked (4C). `@@||fpc.datadome.co^$domain=paypal.com`, as EasyPrivacy writes Venmo's. EasyPrivacy writes such exceptions against the alias host, which the alias check honors. Report drafted for the user.
  - **bild.de walls off its content** ("Aufgrund Ihres Blockers zeigen wir BILD.de nicht an"): blocking is off there (`@@||bild.de^$document`), as if the user had set it Off. Shade doesn't work around anti-adblock walls (contested in Germany); allowing the site was the recommendation the user answered "good" to, and stays theirs to revisit. Shade aborted bild.de's navigation in 2 of 4 tries during a build: see the harness below.
- **Three harness faults:**
  - amazon.com's search "failed": EasyList hides the sponsored results, which come first, and the steps looked at the first match only. Steps now take any visible match.
  - A new tab's own `about:blank` commit can land after the site's navigation started, and abort it: in Shade the cosmetic throttle holds that commit until the engine answers. The first navigation is retried once.
  - zf.ro timed out twice in the run, then loaded in triage: the network.
- **19 sites threw only because a request Shade blocked never answered** (trackers, ad SDKs, analytics loaders, Adobe's edge), with text, height and flows intact: each has a reason in the corpus naming what was blocked.
- **Seen, left alone:** 4C blocks some first-party paths through a CDN alias (walmart.com's PerimeterX collector through `*.akamaiedge.net`, an Osano script through CloudFront) because the lists name those hosts; nothing visible broke.
