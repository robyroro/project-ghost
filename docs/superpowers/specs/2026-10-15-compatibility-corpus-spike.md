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
