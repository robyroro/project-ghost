# The compatibility corpus: design

- Status: done 2026-10-16 ([progress notes](2026-10-15-compatibility-corpus-spike.md)): no broken site at Standard in the last full run, Strict 95.1%. Design approved 2026-10-15; amended while planning ([plan](../plans/2026-10-15-compatibility-corpus.md)): `shade.txt` lives in `components/blocking/`, outside `data/`, whose README and LICENSE are EasyList's, so the credits don't change; `--level off` replaces `--off`; the self-test's sites are built in code, since their URLs carry the server's port. Amended while running: Shade honors the lists' `$document` exceptions (the bild.de fix needs one); a step takes any visible match of its selector; the first navigation is retried once after an abort.
- Phase 4, sub-project 4D-1 ([roadmap](../../roadmap.md#phase-4-content-protections)). 4D is split in two (the user's choice, 2026-10-15): **4D-1** the compatibility corpus, which measures what Shade breaks and fixes it until Phase 4's exit criterion holds; **4D-2** the breakage controls users see (the report flow), shaped by what 4D-1 finds.
- Builds on [3A, the blocking engine](2026-10-10-blocking-engine-design.md), [3D-1, the protection levels](2026-10-10-protection-levels-design.md), and 4A–4C's content protections.

## Goal

Phase 4 exits when the compatibility corpus' pass rate with protections on is no worse than vanilla Chromium's. 4D-1 builds that corpus and the harness that runs it, defines "pass" precisely, and fixes what it finds: with Shade at Standard, no site of the corpus works worse than in the baseline browser.

## Decisions

Settled in discussion on 2026-10-15:

- **What passes: generic signals for every site, scripted flows for some.** Every site is judged on whether it loads, on new uncaught exceptions, and on its visible text and height against the baseline's. About 20 sites also have a flow (search, play a video, open a menu, add to cart, pan a map) that must succeed. Generic signals alone miss a dead button on a page that looks fine; flows alone keep the corpus small; screenshot diffs are noisy with rotating content.
- **About 150 sites, curated by category,** from Tranco's top sites where blocking tends to break things: news, video, shopping, maps, docs and SaaS, social, login pages (logged out), travel, Romanian sites, and the perftest corpus' 20 front pages. Not Tranco's top 500 unfiltered: CDNs and front pages that rarely break would dilute it.
- **The baseline is Chrome for Testing at the pinned version** (152.0.7977.167 today): Google's automation build, fetched from its official endpoint, the exact Chromium version Shade is built from, at no build cost. It is Google-branded with Google's services, which matters only on a few Google sites. Rejected: Shade with protections Off as the baseline, since Shade's other defaults (third-party cookie blocking, GPC) would then never show as breakage; it stays a triage tool. Rejected too: our own vanilla build, about 11 hours per pin and a second patch state.
- **The harness is Python over `tools/cdp.py`** (approach A of three), like the egress audit and the cost tools: no new dependency, unit-tested offline, driving the binary we ship. Rejected: Playwright (a Node toolchain and an npm tree in the repository); browser tests against live sites (network flakiness in the test binaries, and no way to run the baseline).
- **Fixes go to a list of Shade's own,** `shade.txt`, beside EasyList and EasyPrivacy, and to code when Shade is at fault. List bugs are also reported upstream.
- **Standard decides the exit criterion,** the default level of regular windows. Strict runs as a second pass, reported, not gating.
- **Logged out, always.** No flow signs in or enters credentials. A CAPTCHA or bot wall is never solved or worked around: the site is skipped.

## Components

| Unit | What it does |
|---|---|
| `test/compat/corpus.toml` | The sites. Each `[[site]]` has a `url`, a `category`, optionally a `flow` (a list of steps) and a `reason` (below). TOML because `tomllib` is in Python's standard library and hand-written flows need comments. |
| Flow steps | Data, each one action or check: `click` (a selector), `click_if_present` (a consent banner), `type` (a selector and text), `press` (a key), `scroll`, `wait_for` (a selector), `expect_visible` (a selector), `expect_text` (a selector and text). Clicks and keys go through DevTools' input pipeline, as the egress audit's scenarios do. |
| `tools/compat.py run` | `--shade <exe> --baseline <exe> [--launcher hidden.ps1] [--sites ...] [--category ...] [--level standard\|strict] [--off] --out report.json`. Starts both browsers, each on a fresh profile, and runs every site in both at once. `--off` sets each site Off in Shade's profile, for triage. `--self-test` runs `test/compat/site/` instead of the corpus. |
| `tools/compat.py report` | `report.json` to Markdown: the rate per level, then each broken, flaky and skipped site with its reason and both browsers' signals side by side. |
| `tools/compat.py baseline` | Fetches Chrome for Testing for the version in `CHROMIUM_VERSION` from Google's official endpoint into a directory outside the repository, and checks its SHA-256 against `test/compat/baseline.json`. The first fetch for a new pin records the hash there, in a commit of its own. |
| `test/compat/site/` | Loopback pages for the self-test: one whose flow needs a script that a test list blocks, one untouched. |
| `components/blocking/data/shade.txt` | Shade's own list, MPL-2.0, in adblock syntax, loaded after EasyList and EasyPrivacy. In 4D-1 it holds only fixes: each entry is scoped to its site (`$domain=`, an exact path), never generic, with a comment naming the corpus site, the failure, and the upstream report when a list was at fault. |
| Shipping `shade.txt` | One more name in `kFilterListFiles` and in `filter_lists`' copy step; one line in patch 0030 (`chrome.release`); the credits (patch 0031's list). `tools/filter_lists.py`, which refreshes the upstream lists and `VERSIONS.json`, leaves it alone: it isn't fetched. |

## A run

For each site, in both browsers at once (one DevTools session each, a new tab per site):

1. **Load.** Navigate; wait for `load`, then until no request has started for 2 s; at most 30 s in all.
2. **Signals.** Loaded or not; the main document's HTTP status; uncaught exceptions (`Runtime.exceptionThrown`), deduplicated by message; `document.body.innerText`'s length; `document.documentElement.scrollHeight`.
3. **Flow,** if any: each step within 10 s; the first that fails is recorded, with its index.
4. **Close** the tab. The profiles last the whole run, so consent choices stay alike in both.

## Verdicts

Shade is judged against the baseline of the same moment:

- **Skipped:** the baseline didn't load (no 2xx, or a timeout) or failed its flow: the site is down, behind a CAPTCHA or bot wall, or changed. Left out of the rate and listed, so stale flows get noticed.
- **Broken**, when any of these holds:
  - Shade didn't load;
  - Shade's flow failed;
  - Shade has an uncaught exception the baseline doesn't;
  - Shade's text length is under 50% of the baseline's;
  - Shade's height is under 50% of the baseline's.

  The 50% cuts are loose on purpose: hidden ads take text and height away legitimately.
- **Passed** otherwise.
- **Flaky:** a broken site is run once more in both browsers. Broken again, it is broken; passed, it is flaky, counted as passed, and listed.
- **A reason** in the corpus file can mark a broken site as not Shade's breakage, when triage shows it (an exception thrown by a page's fallback for a tracker that was blocked, with nothing visible lost). The report shows the reason; such a site counts as passed. A reason never hides a failed flow.

**The rate** is passed / (passed + broken). Since the baseline passes every site that isn't skipped, Phase 4's criterion becomes: **no broken site at Standard.**

A full run, Standard and Strict, takes about 1.5–2 hours; `--sites` and `--category` narrow it for triage.

## Fixing a broken site

1. **Triage:** `compat.py run --sites <site>`, then again with `--off`. Broken with Off too: one of Shade's other defaults, not blocking.
2. **Fix it as narrowly as possible:**
   - a list rule at fault: an exception or a scriptlet change in `shade.txt` for that site, and a report to EasyList or EasyPrivacy, drafted for the user to send;
   - Shade at fault: a code fix with a test.
3. The site stays in the corpus, so the fix stays covered.

## Errors

- A browser crashes or its DevTools connection drops: the run restarts that browser with a fresh profile and records the site as skipped with the cause, unless it was Shade that crashed: then the site is broken.
- The corpus file is invalid (an unknown step or category, a missing field, a duplicate site): `run` refuses to start and names the entry.
- The baseline's hash differs from `baseline.json`: `baseline` refuses it.

## Security and privacy

- A run visits about 150 live sites from this PC, in throwaway profiles, as a user would. It isn't part of the egress audit, which stays as it is; `shade.txt` adds no host.
- An exception in `shade.txt` weakens blocking on one site. Each is reviewed like code, scoped to its site, and tied to a corpus failure.
- `shade.txt` ships inside the installer like the other lists; from 3E it is signed with them.

## Testing

**The harness** (`tools/tests/test_compat.py`, in tooling CI):
- the corpus: unknown step or category, missing field, duplicate site;
- the verdicts on recorded signals, at each threshold; skipped, flaky and the rerun; reasons;
- the report;
- the baseline's hash check;
- each flow step as the CDP calls it makes, against a fake session.

**The self-test:** `compat.py run --self-test` with both browsers reports the page whose script the test list blocks as broken, and the untouched page as passed: the harness catches breakage end to end.

**Shade:**
- every rule of `shade.txt` parses (a bridge test, as for the shipped resources);
- `ReadFilterLists` reads three lists;
- `installer_smoke` checks that `shade.txt` is installed.

**Mutation checks**, each must fail:
- the verdict ignores a failed flow (unit tests);
- the verdict ignores a new exception (unit tests);
- a skipped site counts in the rate (unit tests);
- `shade.txt` isn't loaded (the bridge test, and a corpus fix stops working).

## Documentation

testing.md (the compatibility layer as built), privacy-model.md (Shade's own list, what it holds now), architecture.md (the third list), roadmap.md (4D-1 done, the run's numbers, Phase 4's exit criterion), progress notes.

## Done when

- [x] The harness' tests pass, the self-test catches the planted breakage, and the mutation checks fail as required.
- [x] A full run on the official build finds no broken site at Standard, after fixes; Strict's result is reported.
- [x] The suites pass with no retry; the egress audit finds no unexpected host.
- [x] The documentation is updated; everything is pushed with tooling CI green.

## Out of scope

- The report flow and any UI (4D-2).
- Scheduled nightly runs: there is no build host yet; the corpus runs by hand, and before each phase exit.
- Strict as a gate; logged-in flows; annoyance and cookie-banner lists.
- Blocking rules of Shade's own (first-party and CNAME-cloaked trackers): a later sub-project.
