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
