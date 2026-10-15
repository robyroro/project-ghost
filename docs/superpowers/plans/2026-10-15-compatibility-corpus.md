# Compatibility Corpus (4D-1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure what Shade at Standard breaks on about 150 real sites against Chrome for Testing at the pin, and fix it with a list of Shade's own until no corpus site is broken ([spec](../specs/2026-10-15-compatibility-corpus-design.md)).

**Architecture:**
- **Harness (Python, standard library):**
  - `tools/compat_corpus.py` reads and validates `test/compat/corpus.toml`;
  - `tools/compat_verdict.py` judges Shade's signals against the baseline's and writes the report;
  - `tools/compat_page.py` drives one tab over `tools/cdp.py`: load, signals, flow steps;
  - `tools/compat.py` is the CLI. It fetches the baseline, starts both browsers, runs every site in both at once, reruns the broken ones, and runs the self-test.
- **Shade:** `components/blocking/shade.txt`, a third list, read after EasyList and EasyPrivacy, copied beside the binaries and packed by patch 0030. A bridge function `list_problems` lets a unit test reject any rule the parser refuses.

**Tech Stack:**
- Python 3.11+ (`tomllib`, `concurrent.futures`, `http.server`, `zipfile`, `hashlib`), `tools/cdp.py`;
- Chrome for Testing 152.0.7977.167 (`win64/chrome-win64.zip`, 203,007,504 bytes);
- adblock-rust 0.13.3 (`adblock::lists::parse_filter`), cxx, gtest.

**Settled while planning** (each checked in the source; the spec is amended in Task 11):
- **`shade.txt` lives in `components/blocking/`, not `data/`.** `data/`'s `README.chromium` and `LICENSE` are EasyList's (GPL-3.0/CC-BY-SA), and `//ghost/build/credits.gni` credits that whole directory in `about:credits`. Shade's own MPL-2.0 list isn't third-party code, so the credits don't change.
- **A list that is empty is "missing".** `ReadFilterLists` logs an error for an empty file (`filter_lists.cc`). So `shade.txt` always starts with its header comments: never empty, even with no fix in it.
- **Triage uses the default level, not per-site prefs.** `--level off` sets `ghost.privacy_policy.default_level` to `off` in Shade's profile. That is every site Off, with no registrable-domain computation in Python, so the spec's `--off` becomes `--level off`.
- **The self-test's blocked script** is `/adobe-analytics.js`, a generic EasyPrivacy rule with no option. It is served from `tracker.compat-other.test`, a third party to the page's `compat.test`, both mapped to loopback with `--host-resolver-rules` in both browsers. HTTP to a `.test` name is fine under HTTPS-First's balanced mode, as `scanner_cost.py` shows.
- **The self-test's sites are built in code** (`self_test_sites(base)`), because their URLs carry the server's random port.
- **Patch 0030 changes by one line in `chrome.release`.** Fix its commit in the Chromium checkout (`--fixup=amend:`, then `--autosquash`), then `patches.py export`. The rebase rewrites the files of the later patches, so the next official build recompiles them (expect 20–60 min, not 5).
- **The main document's status** comes from `performance.getEntriesByType('navigation')[0].responseStatus` (Chromium has it since 109), so the harness needs no `Network.responseReceived` bookkeeping. "No request started for 2 s" counts `Network.requestWillBeSent` events, which `cdp.Session` collects while it waits for a command's reply.

**Conventions** (as in the earlier sub-projects):
- **Work on a branch:** `git switch -c 4d-1` in webops before Task 1. `main` stays releasable (the 2026-10-14 drill's lesson).
- **Edit in webops, build in `chromium/src`.** `python <scratchpad>\sync.py` copies changed files into `src/ghost`. Commit in webops.
- **Python tests:** `python -m unittest discover -s tools/tests -t tools -p "test_compat*.py" -v` for these tasks; the whole suite before each push (`python -m unittest discover -s tools/tests -t tools`).
- **Builds:** `powershell -File <scratchpad>\build.ps1 -Out vanilla -Targets "<targets>"` (development); the official build through `<scratchpad>\release_build.cmd`, started via WMI so it survives the session (copy `release_build_4c.cmd` from session f9fc1bf8's scratchpad and change its targets to `chrome mini_installer`).
- **Browser tests and browsers run only on the hidden desktop:** `bt.ps1`, and `--launcher <scratchpad>\hidden.ps1` for the tools.
- **New files carry the MPL header** (`tools/lint.py` checks `.py`, `.cc`, `.h`, `.rs`, `.gn`, `.js`, `.html`). Stage before `python tools/lint.py`. Write files with LF endings: write bytes, or `newline="\n"`.
- **Commit messages:** in a file (`git commit -F <scratchpad>\msg.txt`), with no AI trailer: the user is the only author.

---

### Task 0: Tools and state

- [ ] **Step 1:** `git switch -c 4d-1` in webops. Check that `git status` is clean and `main` equals `origin/main`.
- [ ] **Step 2:** Copy `sync.py`, `build.ps1`, `bt.ps1`, `hidden.ps1` and `release_build_4c.cmd` (renamed to `release_build.cmd`, targets `chrome mini_installer`) from `…\f9fc1bf8-c867-42f1-838e-d476ae6adbdb\scratchpad\` into this session's scratchpad.
- [ ] **Step 3:** Run `python <scratchpad>\sync.py`. Expected: `copied 0` (the 4C session left `src/ghost` equal to `origin/main`).

---

### Task 1: The corpus file's reader

**Files:**
- Create: `tools/compat_corpus.py`
- Test: `tools/tests/test_compat_corpus.py`

- [ ] **Step 1: Write the failing tests**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

import compat_corpus
from compat_corpus import CorpusError, Site, Step

GOOD = """
[[site]]
url = "https://www.wikipedia.org/"
category = "docs"
flow = [
  { action = "type", selector = "#searchInput", text = "Chromium" },
  { action = "press", key = "Enter" },
  { action = "expect_text", selector = "#firstHeading", text = "Chromium" },
]

[[site]]
url = "https://www.bbc.com/"
category = "perftest"
reason = "a blocked tracker's fallback throws; nothing visible is lost"
"""


class ParseTest(unittest.TestCase):
    def test_reads_sites_flows_and_reasons(self):
        sites = compat_corpus.parse(GOOD)
        self.assertEqual(sites, [
            Site("https://www.wikipedia.org/", "docs", (
                Step("type", selector="#searchInput", text="Chromium"),
                Step("press", key="Enter"),
                Step("expect_text", selector="#firstHeading", text="Chromium"))),
            Site("https://www.bbc.com/", "perftest",
                 reason="a blocked tracker's fallback throws; nothing visible is lost"),
        ])

    def assertRefused(self, text: str, words: str):
        with self.assertRaises(CorpusError) as caught:
            compat_corpus.parse(text)
        self.assertIn(words, str(caught.exception))

    def test_a_site_needs_an_http_url(self):
        self.assertRefused('[[site]]\nurl = "ftp://x.test/"\ncategory = "news"\n', "url")
        self.assertRefused('[[site]]\ncategory = "news"\n', "url")

    def test_an_unknown_category_is_refused(self):
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "blogs"\n',
                           "unknown category 'blogs'")

    def test_a_site_is_listed_once(self):
        one = '[[site]]\nurl = "https://x.test/"\ncategory = "news"\n'
        self.assertRefused(one + one, "listed twice")

    def test_an_unknown_field_is_refused(self):
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "news"\nlevel = 1\n',
                           "unknown field(s) level")

    def test_an_unknown_action_is_refused(self):
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "news"\n'
                           'flow = [{ action = "hover", selector = "a" }]\n',
                           "step 1: unknown action 'hover'")

    def test_an_action_needs_its_fields_and_no_others(self):
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "news"\n'
                           'flow = [{ action = "type", selector = "#q" }]\n', "type needs text")
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "news"\n'
                           'flow = [{ action = "click", selector = "a", text = "x" }]\n',
                           "click takes no text")

    def test_only_known_keys_are_pressed(self):
        self.assertRefused('[[site]]\nurl = "https://x.test/"\ncategory = "news"\n'
                           'flow = [{ action = "press", key = "F5" }]\n', "unknown key 'F5'")

    def test_a_corpus_has_a_site(self):
        self.assertRefused("", "no site")

    def test_not_toml_is_refused(self):
        self.assertRefused("[[site]\n", "not TOML")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run them.** `python -m unittest discover -s tools/tests -t tools -p "test_compat_corpus.py" -v`. Expected: `ModuleNotFoundError: No module named 'compat_corpus'`.

- [ ] **Step 3: Write `tools/compat_corpus.py`**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The compatibility corpus (Phase 4D-1): test/compat/corpus.toml's sites.

Each [[site]] has a url and a category, optionally a flow (steps both
browsers must get through) and a reason, which excuses a broken site once
triage has shown the breakage isn't Shade's (compat_verdict.py). Flows stay
logged out: no step ever enters a credential.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import urlsplit

REPO = Path(__file__).resolve().parent.parent
CORPUS = REPO / "test" / "compat" / "corpus.toml"

CATEGORIES = ("news", "video", "shopping", "maps", "docs", "social", "login", "travel",
              "romanian", "perftest")
# Each action and the fields it takes.
ACTIONS = {
    "click": ("selector",),
    "click_if_present": ("selector",),
    "type": ("selector", "text"),
    "press": ("key",),
    "scroll": (),
    "wait_for": ("selector",),
    "expect_visible": ("selector",),
    "expect_text": ("selector", "text"),
}
KEYS = ("Enter", "Escape", "Tab", "ArrowDown")


class CorpusError(ValueError):
    """The corpus file is wrong; the message names the entry."""


@dataclass(frozen=True)
class Step:
    action: str
    selector: str = ""
    text: str = ""
    key: str = ""


@dataclass(frozen=True)
class Site:
    url: str
    category: str
    flow: tuple[Step, ...] = ()
    reason: str = ""


def parse(text: str) -> list[Site]:
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as e:
        raise CorpusError(f"not TOML: {e}") from e
    unknown = set(data) - {"site"}
    if unknown:
        raise CorpusError(f"unknown table(s) {', '.join(sorted(unknown))}")
    sites: list[Site] = []
    seen: set[str] = set()
    for i, entry in enumerate(data.get("site", [])):
        site = _site(entry, f"site {i + 1}")
        if site.url in seen:
            raise CorpusError(f"site {i + 1}: {site.url} is listed twice")
        seen.add(site.url)
        sites.append(site)
    if not sites:
        raise CorpusError("no site")
    return sites


def load(path: Path = CORPUS) -> list[Site]:
    return parse(path.read_text(encoding="utf-8"))


def _site(entry: dict, where: str) -> Site:
    extra = set(entry) - {"url", "category", "flow", "reason"}
    if extra:
        raise CorpusError(f"{where}: unknown field(s) {', '.join(sorted(extra))}")
    url = entry.get("url")
    parts = urlsplit(url) if isinstance(url, str) else None
    if not parts or parts.scheme not in ("http", "https") or not parts.hostname:
        raise CorpusError(f"{where}: url must be an http(s) URL")
    where = f"{where} ({url})"
    category = entry.get("category")
    if category not in CATEGORIES:
        raise CorpusError(f"{where}: unknown category {category!r}")
    reason = entry.get("reason", "")
    if not isinstance(reason, str):
        raise CorpusError(f"{where}: reason must be text")
    steps = entry.get("flow", [])
    if not isinstance(steps, list):
        raise CorpusError(f"{where}: flow must be a list of steps")
    flow = tuple(_step(step, f"{where} step {j + 1}") for j, step in enumerate(steps))
    return Site(url, category, flow, reason)


def _step(entry: object, where: str) -> Step:
    if not isinstance(entry, dict):
        raise CorpusError(f"{where}: a step is a table")
    action = entry.get("action")
    if action not in ACTIONS:
        raise CorpusError(f"{where}: unknown action {action!r}")
    fields = ACTIONS[action]
    extra = set(entry) - {"action", *fields}
    if extra:
        raise CorpusError(f"{where}: {action} takes no {', '.join(sorted(extra))}")
    for name in fields:
        if not isinstance(entry.get(name), str) or not entry[name]:
            raise CorpusError(f"{where}: {action} needs {name}")
    if action == "press" and entry["key"] not in KEYS:
        raise CorpusError(f"{where}: unknown key {entry['key']!r}")
    return Step(action, entry.get("selector", ""), entry.get("text", ""), entry.get("key", ""))
```

- [ ] **Step 4: Run the tests.** Expected: 10 pass.
- [ ] **Step 5: Commit.** Stage, `python tools/lint.py`, message: `tools: compat_corpus reads the compatibility corpus`.

---

### Task 2: Verdicts, the rate and the report

**Files:**
- Create: `tools/compat_verdict.py`
- Test: `tools/tests/test_compat_verdict.py`

- [ ] **Step 1: Write the failing tests**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

import compat_verdict as v
from compat_verdict import Signals

FINE = Signals(True, 200, ("TypeError: x is undefined",), 5000, 3000)


class JudgeTest(unittest.TestCase):
    def test_the_same_page_passes(self):
        self.assertEqual(v.judge(FINE, FINE), (v.PASSED, []))

    def test_a_baseline_that_fails_skips_the_site(self):
        for baseline, words in ((Signals(False, error="timeout"), "didn't load (timeout)"),
                                (Signals(True, 403, (), 10, 10), "HTTP 403"),
                                (Signals(True, 200, (), 10, 10, failed_step=1, error="no #q"),
                                 "failed step 2 (no #q)")):
            verdict, why = v.judge(FINE, baseline)
            self.assertEqual(verdict, v.SKIPPED)
            self.assertIn(words, why[0])

    def test_shade_not_loading_is_broken(self):
        self.assertEqual(v.judge(Signals(False, error="ERR_BLOCKED_BY_CLIENT"), FINE),
                         (v.BROKEN, ["didn't load (ERR_BLOCKED_BY_CLIENT)"]))

    def test_a_failed_flow_is_broken(self):
        shade = Signals(True, 200, FINE.exceptions, 5000, 3000, failed_step=0, error="no #q")
        self.assertEqual(v.judge(shade, FINE), (v.BROKEN, ["failed step 1 (no #q)"]))

    def test_a_new_exception_is_broken_an_old_one_isnt(self):
        shade = Signals(True, 200, FINE.exceptions + ("ReferenceError: ga is not defined",),
                        5000, 3000)
        self.assertEqual(v.judge(shade, FINE),
                         (v.BROKEN, ["new exception(s): ReferenceError: ga is not defined"]))

    def test_text_and_height_under_half_are_broken(self):
        self.assertEqual(v.judge(Signals(True, 200, FINE.exceptions, 2500, 1500), FINE)[0],
                         v.PASSED)
        verdict, why = v.judge(Signals(True, 200, FINE.exceptions, 2499, 1499), FINE)
        self.assertEqual(verdict, v.BROKEN)
        self.assertEqual(why, ["text 2499 against 5000", "height 1499 against 3000"])


class SettleTest(unittest.TestCase):
    BROKEN_TEXT = (v.BROKEN, ["text 10 against 5000"])

    def test_only_a_broken_site_is_rerun(self):
        self.assertEqual(v.settle((v.PASSED, []), None, ""), (v.PASSED, []))
        with self.assertRaises(ValueError):
            v.settle(self.BROKEN_TEXT, None, "")

    def test_broken_twice_is_broken_then_passing_is_flaky(self):
        self.assertEqual(v.settle(self.BROKEN_TEXT, self.BROKEN_TEXT, ""), self.BROKEN_TEXT)
        self.assertEqual(v.settle(self.BROKEN_TEXT, (v.PASSED, []), ""),
                         (v.FLAKY, ["text 10 against 5000"]))

    def test_a_rerun_the_baseline_fails_is_skipped(self):
        self.assertEqual(v.settle(self.BROKEN_TEXT, (v.SKIPPED, ["the baseline got HTTP 500"]),
                                  ""), (v.SKIPPED, ["the baseline got HTTP 500"]))

    def test_a_reason_excuses_signals_but_never_a_flow_or_a_load(self):
        self.assertEqual(v.settle(self.BROKEN_TEXT, self.BROKEN_TEXT, "ads only"),
                         (v.EXCUSED, ["text 10 against 5000", "excused: ads only"]))
        for why in (["failed step 1 (no #q)"], ["didn't load (timeout)"]):
            self.assertEqual(v.settle((v.BROKEN, why), (v.BROKEN, why), "ads only"),
                             (v.BROKEN, why))


class RateAndReportTest(unittest.TestCase):
    def result(self, url, verdict, why=()):
        return {"url": url, "category": "news", "verdict": verdict, "why": list(why),
                "shade": vars_of(FINE), "baseline": vars_of(FINE)}

    def test_skipped_sites_are_left_out_of_the_rate(self):
        results = [self.result("https://a/", v.PASSED), self.result("https://b/", v.FLAKY),
                   self.result("https://c/", v.EXCUSED), self.result("https://d/", v.BROKEN),
                   self.result("https://e/", v.SKIPPED)]
        self.assertEqual(v.rate(results), 0.75)
        self.assertIsNone(v.rate([self.result("https://e/", v.SKIPPED)]))

    def test_the_report_lists_what_isnt_passed(self):
        report = v.report({"standard": [
            self.result("https://a/", v.PASSED),
            self.result("https://d/", v.BROKEN, ["failed step 1 (no #q)"]),
            self.result("https://e/", v.SKIPPED, ["the baseline got HTTP 500"])]})
        self.assertIn("## Standard: 50.0% (1 of 2), 1 broken", report)
        self.assertIn("- https://d/ (news): failed step 1 (no #q)", report)
        self.assertIn("Shade: loaded, HTTP 200, 1 exception(s), text 5000, height 3000", report)
        self.assertIn("### Skipped\n\n- https://e/ (news): the baseline got HTTP 500", report)
        self.assertNotIn("https://a/", report)

    def test_signals_round_trip_through_json(self):
        self.assertEqual(Signals.from_dict(vars_of(FINE)), FINE)


def vars_of(signals: Signals) -> dict:
    return {**signals.__dict__, "exceptions": list(signals.exceptions)}


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run them.** `python -m unittest discover -s tools/tests -t tools -p "test_compat_verdict.py" -v`. Expected: `ModuleNotFoundError`.

- [ ] **Step 3: Write `tools/compat_verdict.py`**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Judging a corpus site (Phase 4D-1): Shade's signals against the baseline's.

The baseline is Chrome for Testing at the pinned version, loading the same
site at the same moment. A site it can't load, or whose flow it fails, is
skipped. Otherwise Shade is broken when it didn't load, failed the flow, threw
an exception the baseline didn't, or kept under half the baseline's text or
height (hidden ads take some away legitimately). A broken site is run once
more: broken again, it is broken; passed, flaky. A corpus reason excuses
exceptions, text and height, never a failed flow or a page that didn't load.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

PASSED, BROKEN, FLAKY, SKIPPED, EXCUSED = "passed", "broken", "flaky", "skipped", "excused"
SHARE = 0.5
# Breakage no reason can excuse: what a user would see fail.
UNEXCUSABLE = ("didn't load", "failed step")


@dataclass(frozen=True)
class Signals:
    loaded: bool
    status: int = 0  # the main document's HTTP status; 0 when unknown
    exceptions: tuple[str, ...] = ()  # uncaught, one line each, deduplicated
    text_length: int = 0  # document.body.innerText, after the load
    height: int = 0  # document.documentElement.scrollHeight, after the load
    failed_step: int | None = None  # the first flow step that failed (0-based)
    error: str = ""  # why the page didn't load, or why the step failed

    @classmethod
    def from_dict(cls, data: dict) -> "Signals":
        return cls(**{**data, "exceptions": tuple(data["exceptions"])})

    def to_dict(self) -> dict:
        return {**asdict(self), "exceptions": list(self.exceptions)}


def baseline_problem(baseline: Signals) -> str:
    if not baseline.loaded:
        return f"the baseline didn't load ({baseline.error or 'timeout'})"
    if not 200 <= baseline.status < 300:
        return f"the baseline got HTTP {baseline.status}"
    if baseline.failed_step is not None:
        return f"the baseline failed step {baseline.failed_step + 1} ({baseline.error})"
    return ""


def breakage(shade: Signals, baseline: Signals) -> list[str]:
    """What Shade did worse than the baseline; empty when nothing."""
    if not shade.loaded:
        return [f"didn't load ({shade.error or 'timeout'})"]
    found = []
    if shade.failed_step is not None:
        found.append(f"failed step {shade.failed_step + 1} ({shade.error})")
    new = sorted(set(shade.exceptions) - set(baseline.exceptions))
    if new:
        found.append("new exception(s): " + "; ".join(new))
    if shade.text_length < SHARE * baseline.text_length:
        found.append(f"text {shade.text_length} against {baseline.text_length}")
    if shade.height < SHARE * baseline.height:
        found.append(f"height {shade.height} against {baseline.height}")
    return found


def judge(shade: Signals, baseline: Signals) -> tuple[str, list[str]]:
    problem = baseline_problem(baseline)
    if problem:
        return SKIPPED, [problem]
    found = breakage(shade, baseline)
    return (BROKEN, found) if found else (PASSED, [])


def settle(first: tuple[str, list[str]], rerun: tuple[str, list[str]] | None,
           reason: str) -> tuple[str, list[str]]:
    """The site's verdict, from its first run and, if that was broken, its rerun."""
    if first[0] != BROKEN:
        return first
    if rerun is None:
        raise ValueError("a broken site is run again before it is judged")
    verdict, why = rerun
    if verdict == PASSED:
        return FLAKY, first[1]
    if verdict == SKIPPED:
        return rerun
    if reason and not any(w.startswith(UNEXCUSABLE) for w in why):
        return EXCUSED, why + [f"excused: {reason}"]
    return BROKEN, why


def rate(results: list[dict]) -> float | None:
    """Passed (flaky and excused included) over everything not skipped."""
    good = sum(1 for r in results if r["verdict"] in (PASSED, FLAKY, EXCUSED))
    bad = sum(1 for r in results if r["verdict"] == BROKEN)
    return good / (good + bad) if good + bad else None


def _signals_line(name: str, s: dict) -> str:
    if not s["loaded"]:
        return f"{name}: didn't load ({s['error'] or 'timeout'})"
    line = (f"{name}: loaded, HTTP {s['status']}, {len(s['exceptions'])} exception(s), "
            f"text {s['text_length']}, height {s['height']}")
    if s["failed_step"] is not None:
        line += f", failed step {s['failed_step'] + 1} ({s['error']})"
    return line


def report(levels: dict[str, list[dict]]) -> str:
    """Markdown: per level, the rate, then each site that didn't simply pass."""
    out = ["# Compatibility corpus", ""]
    for level, results in levels.items():
        r = rate(results)
        counted = sum(1 for x in results if x["verdict"] != SKIPPED)
        passed = round((r or 0) * counted)
        broken = sum(1 for x in results if x["verdict"] == BROKEN)
        share = "no site counted" if r is None else f"{r * 100:.1f}% ({passed} of {counted})"
        out += [f"## {level.capitalize()}: {share}, {broken} broken", ""]
        for verdict in (BROKEN, EXCUSED, FLAKY, SKIPPED):
            chosen = [x for x in results if x["verdict"] == verdict]
            if not chosen:
                continue
            out += [f"### {verdict.capitalize()}", ""]
            for x in chosen:
                out.append(f"- {x['url']} ({x['category']}): {'; '.join(x['why'])}")
                if verdict != SKIPPED:
                    out.append(f"  - {_signals_line('Shade', x['shade'])}")
                    out.append(f"  - {_signals_line('Baseline', x['baseline'])}")
            out.append("")
    return "\n".join(out).rstrip() + "\n"
```

- [ ] **Step 4: Run the tests.** Expected: 13 pass.
- [ ] **Step 5: Commit.** Message: `tools: compat_verdict judges a site against the baseline`.

---

### Task 3: One tab: load, signals, flow steps

**Files:**
- Create: `tools/compat_page.py`
- Test: `tools/tests/test_compat_page.py`

- [ ] **Step 1: Write the failing tests.** A fake session answers `Runtime.evaluate` from a function of the expression and records every call; a fake clock advances on each sleep.

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

import compat_page
from compat_corpus import Step


class Clock:
    def __init__(self):
        self.now = 0.0

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeSession:
    """Answers Runtime.evaluate with answer(expression); records the rest."""

    def __init__(self, answer, events=()):
        self.answer = answer
        self.calls = []
        self.events = list(events)

    def call(self, method, **params):
        self.calls.append((method, params))
        if method == "Runtime.evaluate":
            return {"result": {"value": self.answer(params["expression"])}}
        if method == "Page.navigate":
            return {"frameId": "1"}
        return {}

    def methods(self):
        return [m for m, _ in self.calls if m != "Runtime.evaluate"]


def answers(**by_marker):
    """An answer function: the first marker the expression contains decides;
    an expression with none of them gets None (an element that isn't there)."""
    def answer(expression):
        for marker, value in by_marker.items():
            if marker in expression:
                return value
        return None
    return answer


class LoadTest(unittest.TestCase):
    def test_waits_for_complete_then_a_quiet_network(self):
        clock = Clock()
        session = FakeSession(answers(readyState=True))
        self.assertEqual(compat_page.load(session, "https://a.test/", clock.time, clock.sleep),
                         (True, ""))
        self.assertEqual(session.methods()[:4],
                         ["Page.enable", "Runtime.enable", "Network.enable", "Page.navigate"])
        self.assertGreaterEqual(clock.now, compat_page.QUIET)

    def test_a_navigation_error_is_not_loaded(self):
        class Failing(FakeSession):
            def call(self, method, **params):
                if method == "Page.navigate":
                    return {"errorText": "net::ERR_NAME_NOT_RESOLVED"}
                return super().call(method, **params)
        clock = Clock()
        self.assertEqual(compat_page.load(Failing(answers()), "https://a.test/", clock.time,
                                          clock.sleep), (False, "net::ERR_NAME_NOT_RESOLVED"))

    def test_a_page_that_never_completes_times_out(self):
        clock = Clock()
        self.assertEqual(compat_page.load(FakeSession(answers(readyState=False)),
                                          "https://a.test/", clock.time, clock.sleep),
                         (False, "timeout"))
        self.assertGreaterEqual(clock.now, compat_page.LOAD_TIMEOUT)


class ExceptionsTest(unittest.TestCase):
    def test_first_lines_deduplicated(self):
        event = lambda text: {"method": "Runtime.exceptionThrown", "params": {
            "exceptionDetails": {"text": "Uncaught", "exception": {"description": text}}}}
        session = FakeSession(answers(), [
            event("TypeError: a\n    at x.js:1"), event("TypeError: a\n    at y.js:2"),
            {"method": "Runtime.exceptionThrown", "params": {"exceptionDetails": {
                "text": "Uncaught SyntaxError"}}},
            {"method": "Network.requestWillBeSent", "params": {}}])
        self.assertEqual(compat_page.exceptions(session),
                         ("TypeError: a", "Uncaught SyntaxError"))


class StepTest(unittest.TestCase):
    def run_step(self, step, answer):
        clock = Clock()
        session = FakeSession(answer)
        compat_page.run_step(session, step, clock.time, clock.sleep)
        return session

    def test_click_dispatches_a_press_and_a_release_at_the_centre(self):
        session = self.run_step(Step("click", selector="#go"),
                                answers(getBoundingClientRect=[10, 20]))
        mouse = [p for m, p in session.calls if m == "Input.dispatchMouseEvent"]
        self.assertEqual([(p["type"], p["x"], p["y"]) for p in mouse],
                         [("mousePressed", 10, 20), ("mouseReleased", 10, 20)])

    def test_type_clicks_then_types_each_character(self):
        session = self.run_step(Step("type", selector="#q", text="ab"),
                                answers(getBoundingClientRect=[1, 2]))
        keys = [p["key"] for m, p in session.calls if m == "Input.dispatchKeyEvent"]
        self.assertEqual(keys, ["a", "a", "b", "b"])

    def test_press_sends_the_named_key(self):
        session = self.run_step(Step("press", key="Enter"), answers())
        keys = [(p["type"], p.get("windowsVirtualKeyCode")) for m, p in session.calls
                if m == "Input.dispatchKeyEvent"]
        self.assertEqual(keys, [("keyDown", 13), ("keyUp", 13)])

    def test_a_missing_element_fails_the_step_after_its_timeout(self):
        with self.assertRaises(compat_page.StepFailed) as caught:
            self.run_step(Step("expect_visible", selector="#nothing"), answers())
        self.assertIn("#nothing", str(caught.exception))

    def test_click_if_present_passes_without_the_element(self):
        session = self.run_step(Step("click_if_present", selector="#consent"),
                                answers())
        self.assertNotIn("Input.dispatchMouseEvent", session.methods())

    def test_expect_text_looks_for_the_text_in_the_element(self):
        session = self.run_step(Step("expect_text", selector="h1", text="Chromium"),
                                answers(innerText=True))
        expression = session.calls[0][1]["expression"]
        self.assertIn('"h1"', expression)
        self.assertIn('"chromium"', expression)
```

- [ ] **Step 2: Run them.** `python -m unittest discover -s tools/tests -t tools -p "test_compat_page.py" -v`. Expected: `ModuleNotFoundError`.

- [ ] **Step 3: Write `tools/compat_page.py`**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""One corpus site in one browser tab, over DevTools (Phase 4D-1).

load() navigates and waits for the load, then for the network to stay quiet;
signals() reads what the verdict needs; run_step() acts as a user would,
through DevTools' input pipeline, so pages see real clicks and key presses.
The clock and sleep are parameters, so the tests run without waiting.
"""

from __future__ import annotations

import json
import time
from typing import Callable

import cdp
from compat_corpus import Step
from egress_audit import key_events

LOAD_TIMEOUT = 30.0  # navigation to a quiet network, at most
QUIET = 2.0  # no request started for this long
STEP_TIMEOUT = 10.0
CONSENT_TIMEOUT = 3.0  # how long click_if_present waits for its element
POLL = 0.25
# Key name: (Windows virtual key code, text it types).
KEYS = {"Enter": (13, "\r"), "Escape": (27, ""), "Tab": (9, "\t"), "ArrowDown": (40, "")}
SIGNALS = ("(() => ({status: performance.getEntriesByType('navigation')[0]?.responseStatus ?? 0,"
           " text: document.body ? document.body.innerText.length : 0,"
           " height: document.documentElement.scrollHeight}))()")

Clock = Callable[[], float]
Sleep = Callable[[float], None]


class StepFailed(Exception):
    """A flow step didn't succeed within its timeout."""


def evaluate(session: cdp.Session, expression: str):
    reply = session.call("Runtime.evaluate", expression=expression, returnByValue=True)
    if "exceptionDetails" in reply:
        raise cdp.ProtocolError(f"{expression}: {reply['exceptionDetails'].get('text')}")
    return reply["result"].get("value")


def poll(session: cdp.Session, expression: str, timeout: float, clock: Clock,
         sleep: Sleep) -> bool:
    deadline = clock() + timeout
    while True:
        try:
            if evaluate(session, expression):
                return True
        except cdp.ProtocolError:
            pass  # the page is between documents
        if clock() >= deadline:
            return False
        sleep(POLL)


def _requests(session: cdp.Session) -> int:
    return sum(1 for e in session.events if e.get("method") == "Network.requestWillBeSent")


def load(session: cdp.Session, url: str, clock: Clock = time.monotonic,
         sleep: Sleep = time.sleep) -> tuple[bool, str]:
    """Navigates; (loaded, error). Loaded once complete, then quiet or out of time."""
    for domain in ("Page", "Runtime", "Network"):
        session.call(f"{domain}.enable")
    reply = session.call("Page.navigate", url=url)
    if reply.get("errorText"):
        return False, reply["errorText"]
    start = clock()
    if not poll(session, "document.readyState === 'complete'", LOAD_TIMEOUT, clock, sleep):
        return False, "timeout"
    seen, quiet_since = _requests(session), clock()
    while clock() - quiet_since < QUIET and clock() - start < LOAD_TIMEOUT:
        sleep(POLL)
        evaluate(session, "0")  # a reply drains the events that came before it
        now = _requests(session)
        if now != seen:
            seen, quiet_since = now, clock()
    return True, ""


def exceptions(session: cdp.Session) -> tuple[str, ...]:
    """The uncaught exceptions so far: each one's first line, once, sorted."""
    found = set()
    for event in session.events:
        if event.get("method") != "Runtime.exceptionThrown":
            continue
        details = event["params"]["exceptionDetails"]
        text = details.get("exception", {}).get("description") or details.get("text", "")
        found.add(text.splitlines()[0][:200] if text else "")
    return tuple(sorted(found))


def signals(session: cdp.Session) -> dict:
    """The main document's status, the page's text length and height."""
    return evaluate(session, SIGNALS)


def _element(selector: str, test: str) -> str:
    return (f"(() => {{ const e = document.querySelector({json.dumps(selector)});"
            f" return !!e && ({test}); }})()")


def _visible(selector: str) -> str:
    return _element(selector, "(r => r.width > 0 && r.height > 0)(e.getBoundingClientRect())"
                              " && getComputedStyle(e).visibility !== 'hidden'")


def _click(session: cdp.Session, selector: str) -> None:
    x, y = evaluate(session, f"(() => {{ const e = document.querySelector("
                             f"{json.dumps(selector)}); e.scrollIntoView({{block: 'center'}});"
                             f" const r = e.getBoundingClientRect();"
                             f" return [r.x + r.width / 2, r.y + r.height / 2]; }})()")
    for kind in ("mousePressed", "mouseReleased"):
        session.call("Input.dispatchMouseEvent", type=kind, x=x, y=y, button="left",
                     clickCount=1)


def _press(session: cdp.Session, key: str) -> None:
    code, text = KEYS[key]
    down = {"type": "keyDown", "key": key, "code": key, "windowsVirtualKeyCode": code}
    if text:
        down.update(text=text, unmodifiedText=text)
    session.call("Input.dispatchKeyEvent", **down)
    session.call("Input.dispatchKeyEvent", type="keyUp", key=key, code=key,
                 windowsVirtualKeyCode=code)


def run_step(session: cdp.Session, step: Step, clock: Clock = time.monotonic,
             sleep: Sleep = time.sleep) -> None:
    """Does one flow step; raises StepFailed when it can't within its timeout."""
    def wait(expression: str, what: str, timeout: float = STEP_TIMEOUT) -> None:
        if not poll(session, expression, timeout, clock, sleep):
            raise StepFailed(f"{step.action}: {what} not within {timeout:g} s")

    if step.action == "click_if_present":
        if poll(session, _visible(step.selector), CONSENT_TIMEOUT, clock, sleep):
            _click(session, step.selector)
            sleep(1.0)  # the banner goes, the page may reload
    elif step.action == "click":
        wait(_visible(step.selector), f"{step.selector} visible")
        _click(session, step.selector)
    elif step.action == "type":
        wait(_visible(step.selector), f"{step.selector} visible")
        _click(session, step.selector)
        for key in key_events(step.text):
            session.call("Input.dispatchKeyEvent", **key)
    elif step.action == "press":
        _press(session, step.key)
    elif step.action == "scroll":
        session.call("Input.dispatchMouseEvent", type="mouseWheel", x=200, y=200, deltaX=0,
                     deltaY=2000)
        sleep(1.0)
    elif step.action == "wait_for":
        wait(_element(step.selector, "true"), f"{step.selector} present")
    elif step.action == "expect_visible":
        wait(_visible(step.selector), f"{step.selector} visible")
    elif step.action == "expect_text":
        wait(_element(step.selector, f"e.innerText.toLowerCase().includes("
                                     f"{json.dumps(step.text.lower())})"),
             f"{step.text!r} in {step.selector}")
    else:
        raise ValueError(f"unknown action {step.action}")
```

- [ ] **Step 4: Run the tests.** Expected: 10 pass. (Both the visibility check and the click's position read `getBoundingClientRect`, so one marker answers both.)
- [ ] **Step 5: Commit.** Message: `tools: compat_page loads a site and acts out its flow`.

---

### Task 4: The CLI: baseline, browsers, run, report

**Files:**
- Create: `tools/compat.py`, `test/compat/baseline.json` (by the first real fetch, Task 6)
- Test: `tools/tests/test_compat.py`

- [ ] **Step 1: Write the failing tests**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import hashlib
import io
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

import compat
import compat_verdict as v
from compat_corpus import Site
from compat_verdict import Signals


def chrome_zip() -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as z:
        z.writestr("chrome-win64/chrome.exe", b"MZ fake")
    return buffer.getvalue()


class BaselineTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.record = self.dir / "baseline.json"
        self.data = chrome_zip()
        self.urls = []

    def download(self, url, path):
        self.urls.append(url)
        path.write_bytes(self.data)

    def fetch(self):
        return compat.fetch_baseline("152.0.7977.167", self.dir / "cft", self.record,
                                     self.download)

    def test_a_new_version_is_recorded_and_unpacked(self):
        exe = self.fetch()
        self.assertEqual(self.urls, ["https://storage.googleapis.com/chrome-for-testing-public/"
                                     "152.0.7977.167/win64/chrome-win64.zip"])
        self.assertEqual(exe.read_bytes(), b"MZ fake")
        self.assertEqual(json.loads(self.record.read_text()),
                         {"version": "152.0.7977.167",
                          "sha256": hashlib.sha256(self.data).hexdigest(),
                          "url": self.urls[0]})

    def test_a_recorded_version_must_match(self):
        self.record.write_text(json.dumps({"version": "152.0.7977.167", "sha256": "0" * 64}))
        with self.assertRaises(compat.BaselineError):
            self.fetch()

    def test_an_unpacked_baseline_is_reused(self):
        self.fetch()
        self.fetch()
        self.assertEqual(len(self.urls), 1)


class ProfileTest(unittest.TestCase):
    def test_the_level_is_shades_default_level(self):
        self.assertEqual(compat.preferences("strict"),
                         {"ghost": {"privacy_policy": {"default_level": "strict"}}})

    def test_browser_arguments(self):
        args = compat.browser_args(Path("C:/x/chrome.exe"), Path("C:/p"), 9400,
                                   ["--host-resolver-rules=MAP a.test 127.0.0.1"])
        self.assertEqual(args, [str(Path("C:/x/chrome.exe")), f"--user-data-dir={Path('C:/p')}",
                                "--no-first-run", "--no-default-browser-check",
                                "--remote-debugging-port=9400",
                                "--host-resolver-rules=MAP a.test 127.0.0.1", "about:blank"])


class RunSiteTest(unittest.TestCase):
    FINE = Signals(True, 200, (), 5000, 3000)

    def run_site(self, shade_runs, baseline_runs, reason=""):
        shade, baseline = iter(shade_runs), iter(baseline_runs)
        return compat.run_site(Site("https://a.test/", "news", reason=reason),
                               lambda site: next(shade), lambda site: next(baseline))

    def test_a_passing_site_runs_once(self):
        result = self.run_site([self.FINE], [self.FINE])
        self.assertEqual((result["verdict"], result["rerun"]), (v.PASSED, None))

    def test_a_broken_site_runs_again(self):
        broken = Signals(False, error="timeout")
        result = self.run_site([broken, self.FINE], [self.FINE, self.FINE])
        self.assertEqual(result["verdict"], v.FLAKY)
        self.assertEqual(result["rerun"]["shade"], self.FINE.to_dict())

    def test_a_browser_that_fails_counts_against_its_side(self):
        def boom(site):
            raise ConnectionError("DevTools closed")
        shade_fails = compat.run_site(Site("https://a.test/", "news"), boom,
                                      lambda site: self.FINE)
        self.assertEqual(shade_fails["verdict"], v.BROKEN)
        self.assertIn("browser: DevTools closed", shade_fails["why"][0])
        baseline_fails = compat.run_site(Site("https://a.test/", "news"),
                                         lambda site: self.FINE, boom)
        self.assertEqual(baseline_fails["verdict"], v.SKIPPED)


class SelfTestTest(unittest.TestCase):
    def test_the_self_test_sites(self):
        blocked, fine = compat.self_test_sites("http://compat.test:8000")
        self.assertEqual(blocked.url, "http://compat.test:8000/blocked.html")
        self.assertEqual(fine.url, "http://compat.test:8000/fine.html")
        self.assertEqual(blocked.flow, fine.flow)

    def test_the_self_test_expects_broken_then_passed(self):
        good = [{"url": "x/blocked.html", "verdict": v.BROKEN},
                {"url": "x/fine.html", "verdict": v.PASSED}]
        self.assertEqual(compat.self_test_problems(good), [])
        bad = [{"url": "x/blocked.html", "verdict": v.PASSED},
               {"url": "x/fine.html", "verdict": v.PASSED}]
        self.assertEqual(compat.self_test_problems(bad),
                         ["x/blocked.html: passed, expected broken"])


class SelectTest(unittest.TestCase):
    SITES = [Site("https://a.test/", "news"), Site("https://b.test/", "video"),
             Site("https://c.test/", "news")]

    def test_by_url_and_category(self):
        self.assertEqual(compat.select(self.SITES, ["https://b.test/"], []), [self.SITES[1]])
        self.assertEqual(compat.select(self.SITES, [], ["news"]),
                         [self.SITES[0], self.SITES[2]])
        self.assertEqual(compat.select(self.SITES, [], []), self.SITES)

    def test_an_unknown_url_is_an_error(self):
        with self.assertRaises(ValueError):
            compat.select(self.SITES, ["https://z.test/"], [])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run them.** `python -m unittest discover -s tools/tests -t tools -p "test_compat.py" -v`. Expected: `ModuleNotFoundError: No module named 'compat'`.

- [ ] **Step 3: Write `tools/compat.py`**

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The compatibility corpus (Phase 4D-1): what Shade breaks that Chromium doesn't.

    compat.py baseline [--dir DIR]
    compat.py run --shade <exe> --baseline <exe> [--launcher hidden.ps1]
                  [--level standard|strict|off ...] [--sites URL ...]
                  [--category C ...] [--self-test] [--out report.json]
    compat.py report report.json

baseline fetches Chrome for Testing at the pinned version (CHROMIUM_VERSION)
from Google's official storage, checks its SHA-256 against
test/compat/baseline.json (recording it for a new pin: commit that), unpacks
it outside the repository and prints its chrome.exe.

run loads every site of test/compat/corpus.toml in Shade and in the baseline
at once, each browser on a fresh profile, Shade at the given level (its
default level; off for triage), and judges Shade against the baseline
(compat_verdict.py). Standard and Strict by default. A broken site is run
once more. --self-test runs test/compat/site/ instead, served on loopback:
the page whose script EasyPrivacy blocks must be broken, the other passed.

--launcher runs each browser through a script that takes -CommandLine (on the
development PC, the hidden-desktop launcher, so no window shows).
"""

from __future__ import annotations

import argparse
import base64
import concurrent.futures
import functools
import hashlib
import http.server
import json
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import zipfile
from pathlib import Path
from typing import Callable

import cdp
import compat_corpus
import compat_page
import compat_verdict as v
from compat_corpus import Site, Step
from compat_verdict import Signals

REPO = Path(__file__).resolve().parent.parent
RECORD = REPO / "test" / "compat" / "baseline.json"
SITE_DIR = REPO / "test" / "compat" / "site"
CFT = "https://storage.googleapis.com/chrome-for-testing-public/{version}/win64/chrome-win64.zip"
DEFAULT_DIR = Path.home() / "shade-compat"
# The self-test's hosts: the page's, and a third party's whose script
# EasyPrivacy's generic /adobe-analytics.js rule blocks.
SELF_TEST_HOSTS = ("compat.test", "tracker.compat-other.test")

Visit = Callable[[Site], Signals]


class BaselineError(RuntimeError):
    """The baseline's download isn't the one recorded."""


def _download(url: str, path: Path) -> None:
    with urllib.request.urlopen(url, timeout=60) as response, open(path, "wb") as out:
        shutil.copyfileobj(response, out, 1 << 20)


def fetch_baseline(version: str, directory: Path, record: Path = RECORD,
                   download: Callable[[str, Path], None] = _download) -> Path:
    """chrome.exe of Chrome for Testing `version`, unpacked under `directory`."""
    exe = directory / version / "chrome-win64" / "chrome.exe"
    if exe.is_file():
        return exe
    directory.mkdir(parents=True, exist_ok=True)
    url = CFT.format(version=version)
    archive = directory / f"chrome-win64-{version}.zip"
    download(url, archive)
    data = archive.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    known = json.loads(record.read_text(encoding="utf-8")) if record.is_file() else {}
    if known.get("version") == version:
        if known.get("sha256") != digest:
            archive.unlink()
            raise BaselineError(f"{url}: SHA-256 {digest}, recorded {known.get('sha256')}")
    else:
        record.write_bytes((json.dumps({"version": version, "sha256": digest, "url": url},
                                       indent=1) + "\n").encode("utf-8"))
        # Google's storage answers a HEAD with the object's MD5 (x-goog-hash):
        # compare it once, by hand, before trusting the first record.
        md5 = base64.b64encode(hashlib.md5(data).digest()).decode("ascii")
        print(f"recorded {version}'s SHA-256 in {record}: commit it. MD5 {md5}: check it "
              f"against `curl -sI {url}`'s x-goog-hash")
    with zipfile.ZipFile(archive) as z:
        z.extractall(directory / version)
    archive.unlink()
    return exe


def preferences(level: str) -> dict:
    """Shade's profile at `level` for every site (3D-4's default level)."""
    return {"ghost": {"privacy_policy": {"default_level": level}}}


def browser_args(exe: Path, profile: Path, port: int, extra: list[str]) -> list[str]:
    return [str(exe), f"--user-data-dir={profile}", "--no-first-run",
            "--no-default-browser-check", f"--remote-debugging-port={port}", *extra,
            "about:blank"]


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _devtools(port: int, path: str, method: str = "GET"):
    request = urllib.request.Request(f"http://127.0.0.1:{port}{path}", method=method)
    with urllib.request.urlopen(request, timeout=10) as response:
        body = response.read()
    try:
        return json.loads(body or b"null")
    except ValueError:
        return body.decode("utf-8", "replace")  # /json/close answers in plain text


class Browser:
    """One browser on a fresh profile, driven over DevTools."""

    def __init__(self, exe: Path, launcher: Path | None, prefs: dict | None,
                 extra: list[str]):
        self.exe, self.launcher, self.prefs, self.extra = exe, launcher, prefs, extra
        self.process = None
        self.profile = None
        self.port = 0

    def start(self) -> None:
        self.profile = Path(tempfile.mkdtemp(prefix="compat-profile-"))
        if self.prefs:
            (self.profile / "Default").mkdir()
            (self.profile / "Default" / "Preferences").write_bytes(
                json.dumps(self.prefs).encode("utf-8"))
        self.port = _free_port()
        args = browser_args(self.exe, self.profile, self.port, self.extra)
        if self.launcher:
            args = ["powershell", "-NoProfile", "-File", str(self.launcher),
                    "-CommandLine", subprocess.list2cmdline(args)]
        self.process = subprocess.Popen(args)
        deadline = time.monotonic() + 60
        while not self.alive():
            if time.monotonic() > deadline:
                raise RuntimeError(f"{self.exe} exposed no DevTools within 60 s")
            time.sleep(0.5)

    def alive(self) -> bool:
        try:
            _devtools(self.port, "/json/version")
            return True
        except OSError:
            return False

    def visit(self, site: Site) -> Signals:
        target = _devtools(self.port, "/json/new?about:blank", method="PUT")
        session = cdp.Session.connect(target["webSocketDebuggerUrl"])
        try:
            loaded, error = compat_page.load(session, site.url)
            if not loaded:
                return Signals(False, error=error, exceptions=compat_page.exceptions(session))
            values = compat_page.signals(session)
            failed, step_error = None, ""
            for i, step in enumerate(site.flow):
                try:
                    compat_page.run_step(session, step)
                except compat_page.StepFailed as e:
                    failed, step_error = i, str(e)
                    break
            return Signals(True, values["status"], compat_page.exceptions(session),
                           values["text"], values["height"], failed, step_error)
        finally:
            session.close()
            try:
                _devtools(self.port, f"/json/close/{target['id']}")
            except OSError:
                pass

    def stop(self) -> None:
        try:
            version = _devtools(self.port, "/json/version")
            cdp.Session.connect(version["webSocketDebuggerUrl"]).call("Browser.close")
        except (OSError, cdp.ProtocolError):
            pass
        try:
            self.process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            self.process.kill()
        shutil.rmtree(self.profile, ignore_errors=True)

    def restart_if_gone(self) -> None:
        if not self.alive():
            self.stop()
            self.start()


def _side(visit: Visit, site: Site) -> Signals:
    """A visit; a browser that fails mid-visit leaves a page that didn't load."""
    try:
        return visit(site)
    except (OSError, cdp.ProtocolError, ConnectionError, KeyError, TypeError) as e:
        return Signals(False, error=f"browser: {e}")


def _both(site: Site, shade: Visit, baseline: Visit) -> tuple[Signals, Signals]:
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        a = pool.submit(_side, shade, site)
        b = pool.submit(_side, baseline, site)
        return a.result(), b.result()


def run_site(site: Site, shade: Visit, baseline: Visit) -> dict:
    """One site's result: both browsers' signals, a rerun if broken, the verdict."""
    s, b = _both(site, shade, baseline)
    first = v.judge(s, b)
    rerun = None
    second = None
    if first[0] == v.BROKEN:
        s2, b2 = _both(site, shade, baseline)
        second = v.judge(s2, b2)
        rerun = {"shade": s2.to_dict(), "baseline": b2.to_dict()}
    verdict, why = v.settle(first, second, site.reason)
    return {"url": site.url, "category": site.category, "verdict": verdict, "why": why,
            "shade": s.to_dict(), "baseline": b.to_dict(), "rerun": rerun}


def run_level(sites: list[Site], level: str, shade_exe: Path, baseline_exe: Path,
              launcher: Path | None, extra: list[str]) -> list[dict]:
    shade = Browser(shade_exe, launcher, preferences(level), extra)
    baseline = Browser(baseline_exe, launcher, None, extra)
    shade.start()
    baseline.start()
    results = []
    try:
        for i, site in enumerate(sites, 1):
            result = run_site(site, shade.visit, baseline.visit)
            results.append(result)
            print(f"[{level} {i}/{len(sites)}] {result['verdict']:>8} {site.url}"
                  + (f": {'; '.join(result['why'])}" if result["why"] else ""), flush=True)
            shade.restart_if_gone()
            baseline.restart_if_gone()
    finally:
        shade.stop()
        baseline.stop()
    return results


def self_test_sites(base: str) -> list[Site]:
    flow = (Step("expect_visible", selector="#ready"),)
    return [Site(f"{base}/blocked.html", "news", flow), Site(f"{base}/fine.html", "news", flow)]


def self_test_problems(results: list[dict]) -> list[str]:
    expected = {"blocked.html": v.BROKEN, "fine.html": v.PASSED}
    problems = []
    for r in results:
        want = expected[r["url"].rsplit("/", 1)[1]]
        if r["verdict"] != want:
            problems.append(f"{r['url']}: {r['verdict']}, expected {want}")
    return problems


def select(sites: list[Site], urls: list[str], categories: list[str]) -> list[Site]:
    unknown = set(urls) - {s.url for s in sites}
    if unknown:
        raise ValueError(f"not in the corpus: {', '.join(sorted(unknown))}")
    return [s for s in sites if (not urls or s.url in urls)
            and (not categories or s.category in categories)]


def _serve(directory: Path) -> http.server.ThreadingHTTPServer:
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), functools.partial(Quiet, directory=str(directory)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("baseline")
    p.add_argument("--dir", type=Path, default=DEFAULT_DIR)
    p = sub.add_parser("run")
    p.add_argument("--shade", type=Path, required=True)
    p.add_argument("--baseline", type=Path, required=True)
    p.add_argument("--launcher", type=Path)
    p.add_argument("--level", action="append", choices=("standard", "strict", "off"))
    p.add_argument("--sites", nargs="*", default=[])
    p.add_argument("--category", nargs="*", default=[])
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--out", type=Path, default=Path("compat-report.json"))
    p = sub.add_parser("report")
    p.add_argument("report", type=Path)
    args = parser.parse_args(argv)

    if args.command == "baseline":
        version = (REPO / "CHROMIUM_VERSION").read_text(encoding="utf-8").strip()
        print(fetch_baseline(version, args.dir))
        return 0
    if args.command == "report":
        data = json.loads(args.report.read_text(encoding="utf-8"))
        sys.stdout.write(v.report(data["levels"]))
        return 0

    levels = args.level or ["standard", "strict"]
    server = None
    extra: list[str] = []
    if args.self_test:
        server = _serve(SITE_DIR)
        port = server.server_address[1]
        extra = ["--host-resolver-rules=" + ", ".join(f"MAP {h} 127.0.0.1"
                                                       for h in SELF_TEST_HOSTS)]
        sites = self_test_sites(f"http://{SELF_TEST_HOSTS[0]}:{port}")
        levels = ["standard"]
    else:
        sites = select(compat_corpus.load(), args.sites, args.category)
    try:
        data = {"shade": str(args.shade), "baseline": str(args.baseline),
                "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                "levels": {level: run_level(sites, level, args.shade, args.baseline,
                                            args.launcher, extra) for level in levels}}
    finally:
        if server:
            server.shutdown()
    args.out.write_bytes((json.dumps(data, indent=1) + "\n").encode("utf-8"))
    sys.stdout.write(v.report(data["levels"]))
    if args.self_test:
        problems = self_test_problems(data["levels"]["standard"])
        for problem in problems:
            print(f"SELF-TEST FAILED: {problem}")
        return 1 if problems else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests.** Expected: 12 pass. Then run all `test_compat*.py`: 45 pass.
- [ ] **Step 5: Commit.** Message: `tools: compat runs the corpus in Shade and the baseline`.

---

### Task 5: The self-test pages

**Files:**
- Create: `test/compat/site/blocked.html`, `test/compat/site/fine.html`, `test/compat/site/adobe-analytics.js`, `test/compat/site/ready.js`

- [ ] **Step 1: Check the rule.** `grep -n "^/adobe-analytics.js$" components/blocking/data/easyprivacy.txt`. Expected: one line. If a list refresh removed it, choose another generic `^/<name>.js$` rule with no option in either list, and rename the script and the pages' reference.
- [ ] **Step 2: The pages.** `blocked.html`. The server's port is random, so an inline script writes the script tag with the page's own port:

```html
<!doctype html>
<!-- This Source Code Form is subject to the terms of the Mozilla Public
     License, v. 2.0. If a copy of the MPL was not distributed with this
     file, You can obtain one at https://mozilla.org/MPL/2.0/. -->
<meta charset="utf-8">
<title>Compatibility self-test: a blocked script</title>
<!-- For tools/compat.py --self-test: the page needs a script from a third
     party whose path EasyPrivacy blocks (/adobe-analytics.js), so Shade at
     Standard must be judged broken against the baseline. -->
<h1>A page that needs its script</h1>
<p>The script adds a line below; without it, the flow fails.</p>
<script>
  document.write('<script src="http://tracker.compat-other.test:' + location.port +
                 '/adobe-analytics.js"><\/script>');
</script>
```

`fine.html` is the same, except: the title "Compatibility self-test: a script that loads", the comment "the same page with a script no list blocks (/ready.js): it must pass", and `/ready.js` in the tag.

`adobe-analytics.js` and `ready.js`, identical bodies:

```js
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// tools/compat.py --self-test: what the page needs from its script.
const ready = document.createElement('p');
ready.id = 'ready';
ready.textContent = 'The script ran.';
document.body.appendChild(ready);
```

- [ ] **Step 3: Commit.** Message: `test: the compatibility corpus' self-test pages`.

---

### Task 6: The baseline and the first self-test

- [ ] **Step 1: Ask the user before downloading.** Chrome for Testing 152.0.7977.167, `chrome-win64.zip`, 203,007,504 bytes (about 194 MiB), from `storage.googleapis.com/chrome-for-testing-public` (Google's official CfT storage), unpacked into `%USERPROFILE%\shade-compat\`. Go on only after a clear yes.
- [ ] **Step 2: Fetch it.** `python tools\compat.py baseline`. Expected: `recorded 152.0.7977.167's SHA-256 … MD5 PYUXWGdxnWPydUNE9bSw4g==`, then the path of `chrome.exe`. The MD5 must equal Google's `x-goog-hash` (`md5=PYUXWGdxnWPydUNE9bSw4g==` when planning; check again with `curl -sI`). If it differs, delete `test/compat/baseline.json` and the download, and stop. Record both hashes in the progress notes.
- [ ] **Step 3: The official build.** Shade's `out\release` (from 4C, 152.0.7977.167) is current, since only Python changed. Check `out\release\chrome.exe`'s date, and `out\release\blocking\` for `easylist.txt` and `easyprivacy.txt`.
- [ ] **Step 4: The self-test.** `python tools\compat.py run --shade chromium\src\out\release\chrome.exe --baseline <cft chrome.exe> --launcher <scratchpad>\hidden.ps1 --self-test --out <scratchpad>\selftest.json`. Expected:
  - `blocked.html` broken: failed step 1, `#ready visible not within 10 s`;
  - `fine.html` passed;
  - exit 0.

  If something else, use superpowers:systematic-debugging. Look first at a baseline that didn't load, the host rules, and HTTPS-First.
- [ ] **Step 5: Commit** `test/compat/baseline.json`. Message: `test: Chrome for Testing 152.0.7977.167, the compatibility baseline`. Then start the progress notes, `docs/superpowers/specs/2026-10-15-compatibility-corpus-spike.md`, with the self-test's result, and commit them too (`docs: 4D-1 progress notes`).

---

### Task 7: Shade's own list, shipped

**Files:**
- Create: `components/blocking/shade.txt`
- Modify: `components/blocking/rust/lib.rs`, `components/blocking/filter_lists.h`, `components/blocking/BUILD.gn`, `components/blocking/engine_bridge_unittest.cc`, `components/blocking/filter_lists_unittest.cc`, `tools/installer_smoke.py`, `tools/tests/test_installer_smoke.py`
- Modify (Chromium checkout, then export): `chrome/installer/mini_installer/chrome.release` → `patches/0030-…`

- [ ] **Step 1: Write the failing tests.** In `engine_bridge_unittest.cc`, after `OnlyFunctionStyleScriptletsAreAccepted` (add `#include "base/files/file_util.h"` and `#include "ghost/components/blocking/filter_lists.h"` if absent):

```cpp
// Phase 4D-1: Shade's own list, checked rule by rule.
TEST(EngineBridgeTest, ListProblemsNamesTheLinesTheParserRefuses) {
  rust::Vec<rust::String> problems =
      list_problems("! a comment\n[Adblock Plus 2.0]\n\n||a.test^\n||b.test^$nosuchoption\n");
  ASSERT_EQ(problems.size(), 1u);
  EXPECT_TRUE(std::string(problems[0]).starts_with("line 5: ||b.test^$nosuchoption"))
      << std::string(problems[0]);
}

TEST(EngineBridgeTest, ShadesListHasNoProblem) {
  std::string list;
  ASSERT_TRUE(base::ReadFileToString(DefaultFilterListsDir().AppendASCII("shade.txt"), &list));
  for (const rust::String& problem : list_problems(list)) {
    ADD_FAILURE() << std::string(problem);
  }
}
```

In `filter_lists_unittest.cc`, after `TheBuildCopiesTheShippedLists` (whose comment should name patch 0030, not 0029):

```cpp
TEST(FilterListsTest, ShadesOwnListIsReadLast) {
  const std::vector<std::string> lists = ReadFilterLists(DefaultFilterListsDir());
  ASSERT_FALSE(lists.empty());
  EXPECT_TRUE(lists.back().starts_with("! Title: Shade's fixes")) << lists.back().substr(0, 80);
}
```

- [ ] **Step 2: `shade.txt`**, with LF endings:

```
! Title: Shade's fixes
! This Source Code Form is subject to the terms of the Mozilla Public
! License, v. 2.0. If a copy of the MPL was not distributed with this
! file, You can obtain one at https://mozilla.org/MPL/2.0/.
!
! Shade's own list, read after EasyList and EasyPrivacy (Phase 4D-1). It
! holds only fixes for sites the compatibility corpus (test/compat/) found
! broken. Each entry is scoped to its site, never generic, and its comment
! names the corpus site, the failure, and the upstream report when a list
! was at fault. This header keeps the file from ever being empty, which
! the browser would take for a missing list.
```

- [ ] **Step 3: Ship it.**
  - `filter_lists.h`: `inline constexpr const char* kFilterListFiles[] = {"easylist.txt", "easyprivacy.txt", "shade.txt"};` with a comment: `// In this order; shade.txt, Shade's own fixes (Phase 4D-1), last.`
  - `BUILD.gn` `copy("filter_lists")`: add `"shade.txt",` to `sources` and extend the comment: "and Shade's own list (shade.txt)".
  - `installer_smoke.py`: `FILTER_LISTS = ("easylist.txt", "easyprivacy.txt", "shade.txt")`. In `test_installer_smoke.py`, `INSTALLED["filter_lists"]` gets `"shade.txt"`, and the missing-list test expects two failures:

```python
    def test_the_filter_lists_are_installed_beside_the_browser(self):
        # patches/0030 packs them; without them the browser blocks nothing.
        failures = self.failures(filter_lists=["easylist.txt"])
        self.assertEqual(failures, ["filter list easyprivacy.txt is not in "
                                    "152.0.7977.14001\\blocking",
                                    "filter list shade.txt is not in "
                                    "152.0.7977.14001\\blocking"])
```

- [ ] **Step 4: `list_problems` in the bridge.** In `lib.rs`'s `extern "Rust"` block, after `resource_problem_for_testing`:

```rust
        /// Each line of `list` the parser refuses, as "line N: <rule>:
        /// <error>"; comments, headers and blank lines are fine. For Shade's
        /// own list, which is checked rule by rule (Phase 4D-1).
        fn list_problems(list: &str) -> Vec<String>;
```

and after `fn resource_problem_for_testing`:

```rust
fn list_problems(list: &str) -> Vec<String> {
    list.lines()
        .enumerate()
        .filter(|(_, line)| {
            let line = line.trim();
            !line.is_empty() && !line.starts_with('!') && !line.starts_with('[')
        })
        .filter_map(|(i, line)| {
            adblock::lists::parse_filter(line, false, ParseOptions::default())
                .err()
                .map(|e| format!("line {}: {}: {}", i + 1, line.trim(), e))
        })
        .collect()
}
```

- [ ] **Step 5: Patch 0030.** In `chromium\src` on `ghost/152.0.7977.167`:
  1. Find the commit: `git log --oneline --grep "pack Shade's filter lists"`.
  2. In `chrome/installer/mini_installer/chrome.release`, after `blocking\easyprivacy.txt: %(VersionDir)s\blocking\`, add `blocking\shade.txt: %(VersionDir)s\blocking\`. Change the comment above it to `# Shade's filter lists: //ghost/components/blocking/data and Shade's own` / `# list, copied beside the binaries by //ghost/components/blocking:filter_lists.`
  3. Write the new message to `<scratchpad>\msg0030.txt`: a first line `amend! mini_installer: pack Shade's filter lists beside the browser`, a blank line, then the patch's whole message with its first paragraph saying "EasyList, EasyPrivacy and Shade's own list (shade.txt)" instead of "EasyList and EasyPrivacy". Keep `Why:`, `Upstream:` and `Signed-off-by:` as they are.
  4. `git add chrome/installer/mini_installer/chrome.release` then `git commit --fixup=amend:<sha> -F <scratchpad>\msg0030.txt`.
  5. `set GIT_SEQUENCE_EDITOR=true` (PowerShell: `$env:GIT_SEQUENCE_EDITOR='true'`), then `git rebase -i --autosquash refs/tags/152.0.7977.167`. It isn't interactive with that editor.
  6. From webops: `python tools\patches.py export --src chromium\src`. Expected: `git diff --stat patches/` shows only `0030` changed (two lines and the message). `python tools\patches.py check` passes.
- [ ] **Step 6: Build and run the unit tests** (development build): `python <scratchpad>\sync.py`, then `build.ps1 -Out vanilla -Targets "ghost_unittests"`, then `out\vanilla\ghost_unittests.exe --gtest_filter=EngineBridgeTest.*:FilterListsTest.*`. Expected: all pass, the two new and the old.
  - Check that the tests can fail: put `||b.test^$nosuchoption` into `shade.txt` and sync; `ShadesListHasNoProblem` must fail. Then remove `"shade.txt"` from `kFilterListFiles`; `ShadesOwnListIsReadLast` must fail. Restore both, sync, and rebuild.
  - If `$nosuchoption` parses in adblock 0.13.3 (no error), use a malformed rule the parser refuses (try `||b.test^$domain=` and `##`), and make the test's line number match.
- [ ] **Step 7: Python and lint.** `python -m unittest discover -s tools/tests -t tools -p "test_installer_smoke.py"` passes. Stage, then `python tools/lint.py`.
- [ ] **Step 8: Commit.** Message: `blocking: Shade's own list, shade.txt, read after EasyList and EasyPrivacy`, with a body naming `list_problems` and patch 0030's change.

---

### Task 8: The corpus

**Files:**
- Create: `test/compat/corpus.toml`
- Modify: `tools/tests/test_compat_corpus.py` (a test of the shipped corpus)

- [ ] **Step 1: Write the test of the shipped corpus** in `test_compat_corpus.py`:

```python
class ShippedCorpusTest(unittest.TestCase):
    def test_the_corpus_loads_and_covers_every_category(self):
        sites = compat_corpus.load()
        self.assertGreaterEqual(len(sites), 140)
        self.assertGreaterEqual(sum(1 for s in sites if s.flow), 20)
        self.assertEqual({s.category for s in sites}, set(compat_corpus.CATEGORIES))
```

Run it. Expected: `FileNotFoundError`.

- [ ] **Step 2: Write `test/compat/corpus.toml`.** Its header:

```toml
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The compatibility corpus (Phase 4D-1; docs/testing.md, tools/compat.py):
# sites where blocking tends to break things, by category. A flow is what a
# logged-out user does there; both browsers must get through it. Never a
# step that signs in or enters a credential. A `reason` excuses a broken
# site once triage showed the breakage isn't Shade's (never a failed flow).
```

The sites, one `[[site]]` each (`url`, `category`), in this order:
- **perftest (20):** the 20 front pages of `components/blocking/test/request_corpus.tsv` (`cut -f2 … | sort -u`).
- **news (19):** `https://www.npr.org/`, `https://www.aljazeera.com/`, `https://www.bloomberg.com/`, `https://www.wsj.com/`, `https://www.theatlantic.com/`, `https://www.huffpost.com/`, `https://www.businessinsider.com/`, `https://www.lefigaro.fr/`, `https://www.corriere.it/`, `https://elpais.com/`, `https://www.zeit.de/`, `https://www.bild.de/`, `https://www.politico.com/`, `https://www.theverge.com/`, `https://arstechnica.com/`, `https://techcrunch.com/`, `https://www.wired.com/`, `https://www.espn.com/`, `https://weather.com/`.
- **video (14):** `https://www.youtube.com/`, `https://vimeo.com/`, `https://www.twitch.tv/`, `https://www.dailymotion.com/`, `https://www.ted.com/talks`, `https://www.netflix.com/`, `https://www.primevideo.com/`, `https://www.disneyplus.com/`, `https://www.hulu.com/`, `https://www.crunchyroll.com/`, `https://www.imdb.com/`, `https://open.spotify.com/`, `https://soundcloud.com/`, `https://www.bbc.co.uk/iplayer`.
- **shopping (20):** `https://www.amazon.com/`, `https://www.amazon.de/`, `https://www.ebay.com/`, `https://www.walmart.com/`, `https://www.target.com/`, `https://www.bestbuy.com/`, `https://www.etsy.com/`, `https://www.aliexpress.com/`, `https://www.ikea.com/us/en/`, `https://www.zalando.de/`, `https://www.homedepot.com/`, `https://www.costco.com/`, `https://www.temu.com/`, `https://www.shein.com/`, `https://www.newegg.com/`, `https://www.wayfair.com/`, `https://www.macys.com/`, `https://www.nike.com/`, `https://www.telus.com/` (4C's cloaked Adobe tracker), `https://www.apple.com/`.
- **maps (6):** `https://www.google.com/maps`, `https://www.openstreetmap.org/`, `https://www.bing.com/maps`, `https://wego.here.com/`, `https://www.waze.com/live-map/`, `https://www.mapquest.com/`.
- **docs (22):** `https://www.wikipedia.org/`, `https://en.wikipedia.org/wiki/Web_browser`, `https://github.com/`, `https://stackoverflow.com/questions`, `https://developer.mozilla.org/en-US/`, `https://docs.python.org/3/`, `https://learn.microsoft.com/en-us/`, `https://www.notion.com/`, `https://slack.com/`, `https://www.atlassian.com/`, `https://trello.com/`, `https://www.figma.com/`, `https://www.canva.com/`, `https://www.dropbox.com/`, `https://zoom.us/`, `https://www.salesforce.com/`, `https://www.hubspot.com/`, `https://medium.com/`, `https://duckduckgo.com/`, `https://www.bing.com/`, `https://www.google.com/`, `https://chatgpt.com/`.
- **social (12):** `https://www.reddit.com/`, `https://x.com/`, `https://www.facebook.com/`, `https://www.instagram.com/`, `https://www.linkedin.com/`, `https://www.pinterest.com/`, `https://www.tiktok.com/explore`, `https://www.tumblr.com/`, `https://www.quora.com/`, `https://mastodon.social/explore`, `https://bsky.app/`, `https://www.threads.com/`.
- **login (13), the logged-out sign-in pages only:** `https://accounts.google.com/`, `https://login.microsoftonline.com/`, `https://login.live.com/`, `https://www.paypal.com/signin`, `https://www.chase.com/`, `https://www.bankofamerica.com/`, `https://www.wellsfargo.com/`, `https://account.apple.com/`, `https://github.com/login`, `https://id.atlassian.com/login`, `https://www.revolut.com/`, `https://www.ing.ro/`, `https://www.bancatransilvania.ro/`.
- **travel (13):** `https://www.booking.com/`, `https://www.airbnb.com/`, `https://www.expedia.com/`, `https://www.tripadvisor.com/`, `https://www.skyscanner.net/`, `https://www.kayak.com/`, `https://www.ryanair.com/`, `https://wizzair.com/`, `https://www.lufthansa.com/`, `https://www.hotels.com/`, `https://www.trivago.com/`, `https://www.uber.com/`, `https://www.jalan.net/` (4C's cloaked Adobe tracker).
- **romanian (16):** `https://www.emag.ro/`, `https://www.olx.ro/`, `https://altex.ro/`, `https://www.cel.ro/`, `https://www.gsp.ro/`, `https://www.prosport.ro/`, `https://www.protv.ro/`, `https://stirileprotv.ro/`, `https://www.antena3.ro/`, `https://www.realitatea.net/`, `https://www.g4media.ro/`, `https://www.zf.ro/`, `https://www.mediafax.ro/`, `https://www.anaf.ro/`, `https://www.cfrcalatori.ro/`, `https://www.fancourier.ro/`.

That is 155 sites. **Flows** (at least 20). Write these first. Each selector is a starting guess, checked in Step 3:

```toml
[[site]]
url = "https://www.wikipedia.org/"
category = "docs"
flow = [
  { action = "type", selector = "#searchInput", text = "Chromium" },
  { action = "press", key = "Enter" },
  { action = "expect_text", selector = "#firstHeading", text = "Chromium" },
]
```

The others, as `flow` arrays on their sites:
- `duckduckgo.com`: type `input[name=q]` "privacy browser", Enter, expect_visible `[data-testid=result]`;
- `www.bing.com`: type `#sb_form_q` "chromium", Enter, expect_visible `#b_results .b_algo`;
- `www.google.com`: click_if_present `#L2AGLb` (EU consent), type `textarea[name=q]` "chromium", Enter, expect_visible `#search a h3`;
- `www.youtube.com`: click_if_present `button[aria-label^="Accept"]`, type `input[name=search_query]` "chromium", Enter, expect_visible `ytd-video-renderer`;
- `www.twitch.tv`: expect_visible `video`;
- `www.imdb.com`: type `#suggestion-search` "The Matrix", Enter, expect_visible `.ipc-metadata-list-summary-item`;
- `www.amazon.com`: type `#twotabsearchtextbox` "usb cable", Enter, expect_visible `[data-component-type="s-search-result"]`;
- `www.ebay.com`: type `#gh-ac` "usb cable", Enter, expect_visible `.srp-results li`;
- `www.etsy.com`: type `#global-enhancements-search-query` "mug", Enter, expect_visible `[data-search-results]`;
- `www.emag.ro`: click_if_present `.js-accept`, type `#searchboxTrigger` "telefon", Enter, expect_visible `.card-item`;
- `www.olx.ro`: click_if_present `#onetrust-accept-btn-handler`, type `input[data-testid=search-input]` "bicicleta", Enter, expect_visible `[data-cy=l-card]`;
- `www.openstreetmap.org`: type `#query` "Cluj-Napoca", Enter, expect_visible `.search_results_entry li`;
- `www.google.com/maps`: click_if_present `form[action*="consent"] button`, expect_visible `#searchboxinput`;
- `stackoverflow.com/questions`: expect_visible `#questions .s-post-summary`;
- `github.com/login`: expect_visible `#login_field` (the form shows; nothing is typed);
- `www.reddit.com`: expect_visible `shreddit-post`;
- `developer.mozilla.org/en-US/`: click `button.search-button` (or the search input), type `input[type=search]` "fetch", expect_visible `.result-container`;
- `www.booking.com`: click_if_present `#onetrust-accept-btn-handler`, type `input[name=ss]` "Cluj-Napoca", expect_visible `[data-testid=autocomplete-results-options] li`;
- `www.cfrcalatori.ro`: expect_visible `form` (the timetable search form);
- `www.bbc.com`: scroll, expect_visible `main`;
- `www.nytimes.com`: scroll, expect_visible `main section`.

- [ ] **Step 3: Check each flow against the baseline alone.** For each site with a flow: `compat.py run --shade <cft chrome.exe> --baseline <cft chrome.exe> --level standard --sites <url> --launcher …`. With CfT on both sides, Shade's verdicts mean nothing; what counts is that the baseline passes its flow. A baseline that fails a step means a wrong selector or a consent wall:
  - open the site in the built-in browser and find the element (`find`, `read_page`);
  - correct the selector, or add a `click_if_present` for the consent button;
  - if consent sits in a cross-origin iframe (Sourcepoint, TrustArc), which `querySelector` can't reach, move the flow to another site of the category.

  Stop when at least 20 flows pass in the baseline. Note the replacements in the progress notes.
- [ ] **Step 4: Run the shipped-corpus test.** Expected: pass.
- [ ] **Step 5: Commit.** Message: `test: the compatibility corpus, 155 sites and their flows`.

---

### Task 9: The full run, triage and fixes

- [ ] **Step 1: The official build with `shade.txt`.** `python <scratchpad>\sync.py`, then start `<scratchpad>\release_build.cmd` via WMI (`Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{CommandLine='cmd /c <scratchpad>\release_build.cmd > <scratchpad>\release_build.log 2>&1'}`). Wait for `exit 0` in the log (the rebase of Task 7 makes this longer than usual). Kill any leftover `out\release` `chrome.exe` first: a locked `chrome.dll` fails the link. Check that `out\release\blocking\shade.txt` exists.
- [ ] **Step 2: The full run.** Close other work and don't run `git` in `chromium\src` meanwhile (the app's git polling loads the CPU). Then `python tools\compat.py run --shade chromium\src\out\release\chrome.exe --baseline <cft> --launcher <scratchpad>\hidden.ps1 --out <scratchpad>\run1.json`, in the background: about 1.5–2 h. Then `compat.py report <scratchpad>\run1.json > <scratchpad>\run1.md`.
- [ ] **Step 3: Triage each broken site at Standard** (superpowers:systematic-debugging for each one that isn't obvious):
  1. `compat.py run … --sites <url> --level standard --level off`. If it's broken with Off too, the cause is one of Shade's other defaults (third-party cookies, GPC, HTTPS-First), not blocking. Record it; the fix is a code or default decision for the user, raised with them, not a list entry.
  2. Broken at Standard only: find the rule. Run Shade with `--vmodule=request_filter=1`, which logs `Blocked <url> by <rule>`, through `--launcher`. For cosmetic or scriptlet breakage, try with the request blocked but cosmetics off, by elimination.
  3. The fix in `shade.txt`, as narrow as possible:
     - a blocked request: `@@||host^$script,domain=site.example`;
     - a scriptlet: `site.example#@#+js(name, args)`;
     - a hiding rule: `site.example#@#selector`.

     Each fix gets a comment: `! <url>: <failure>; <rule> in <list> (reported: <link or "drafted">)`.
  4. Draft the upstream report for EasyList or EasyPrivacy (the site, the rule, what breaks, the proposed fix) in `<scratchpad>\upstream-reports.md`, for the user to send. Never post it yourself.
  5. A Shade code bug: stop and fix it under superpowers:test-driven-development, with a browser test.
  6. Breakage that triage shows isn't Shade's (an exception from a blocked tracker's fallback, with nothing visible lost): a `reason` in `corpus.toml`, quoted in the notes.
- [ ] **Step 4: Rebuild and rerun the fixed sites.** `sync.py`, the official build (minutes now), then `compat.py run … --sites <each fixed url>`. Expected: each passed. Then rerun the whole corpus (`run2.json`). Repeat Steps 3–4 until Standard has **no broken site**.
- [ ] **Step 5: Strict.** Read Strict's result from the last full run and record its rate and broken sites in the progress notes. No fix is required for Strict.
- [ ] **Step 6: Commit** `shade.txt`, `corpus.toml` and the progress notes (the runs' rates, each broken site, its cause and fix, flaky and skipped sites, Strict). Messages: `blocking: shade.txt fixes <n> corpus sites`; `docs: 4D-1 corpus runs`.

---

### Task 10: Mutation checks, suites, egress

- [ ] **Step 1: The harness's mutation checks.** Apply each in turn, run `python -m unittest discover -s tools/tests -t tools -p "test_compat*.py"`, and restore with `git checkout -- <file>`. Each must fail:
  - **H1** `compat_verdict.breakage` ignores `failed_step` (delete its `if`): `test_a_failed_flow_is_broken` fails.
  - **H2** it ignores new exceptions: `test_a_new_exception_is_broken_an_old_one_isnt` fails.
  - **H3** `rate` counts `SKIPPED` as good: `test_skipped_sites_are_left_out_of_the_rate` fails.
  - **H4** `settle` lets a reason excuse a failed flow (drop the `UNEXCUSABLE` check): `test_a_reason_excuses_signals_but_never_a_flow_or_a_load` fails.
- [ ] **Step 2: Shade's.**
  - **S1** `kFilterListFiles` without `shade.txt` (development build, `ghost_unittests`): `FilterListsTest.ShadesOwnListIsReadLast` fails.
  - **S2** the same in the official build: a site Task 9 fixed with an exception is broken again (`compat.py run --sites <it>`).
  - **S3** a malformed rule in `shade.txt`: `ShadesListHasNoProblem` fails.

  Restore, sync, rebuild. Compare `src/ghost` with webops (`cmp` over `git ls-files`).
- [ ] **Step 3: The self-test again** on the final official build: exit 0.
- [ ] **Step 4: The whole suites.** `ghost_unittests` and `ghost_browsertests` (development build, hidden desktop), with no retry. Expected: 164 unit tests (161 + 3), 140 browser tests. Record the counts.
- [ ] **Step 5: The egress audit** on the official build, as in 4C (`python tools\egress_audit.py run --chrome chromium\src\out\release\chrome.exe …`, hidden desktop). Expected: no unexpected host.
- [ ] **Step 6: Installer smoke.** `mini_installer` from the official build, then `python tools\installer_smoke.py …` as in docs/testing.md. Expected: pass, with `shade.txt` installed.
- [ ] **Step 7: Commit the notes.** Message: `docs: 4D-1 mutation checks, suites, audit`.

---

### Task 11: Documentation and push

**Files:**
- Modify: `docs/testing.md`, `docs/privacy-model.md`, `docs/architecture.md`, `docs/roadmap.md`, `docs/superpowers/specs/2026-10-15-compatibility-corpus-design.md`, this plan's header

- [ ] **Step 1: testing.md.**
  - The table's Compatibility row: "By hand, before each phase exit; nightly once a build host runs it".
  - A **Compatibility corpus** entry under the notes: `tools/compat.py` (baseline, run, report, self-test), the corpus file and its categories, the verdicts and their thresholds, the rerun, reasons, `--level off` for triage, the harness's tests, H1–H4 and S1–S3, the run time.
- [ ] **Step 2: privacy-model.md**, under Blocking's **Lists**: our own list now ships as `shade.txt`, with only fixes for sites the corpus found broken, each scoped to one site. First-party and CNAME-cloaked trackers of our own come later.
- [ ] **Step 3: architecture.md.** In 3A's "as built" paragraph: three lists, the third Shade's own (`components/blocking/shade.txt`, outside `data/`, whose README and LICENSE are EasyList's), packed by patch 0030. `list_problems` in the bridge.
- [ ] **Step 4: roadmap.md.** Split item 4 into **4D-1** (done, with its date, the rate, the fixes, the checks, the limitations) and **4D-2** (breakage controls: the report flow). Check Phase 4's exit criterion box, with a link to the run's notes. Phase 4 completes only after 4D-2.
- [ ] **Step 5: The spec and the plan.**
  - The spec: status "done <date>", its boxes checked. Add an *amended while planning* note: `shade.txt` in `components/blocking/`, the credits unchanged, `--level off` for `--off`, the self-test's sites built in code.
  - This plan: a **Status** line, as 4C's has.
- [ ] **Step 6: Commit, merge, push.** Stage `docs`, run lint and the whole tooling suite, then commit (`docs: the compatibility corpus (4D-1) done`). Fast-forward `main` to `4d-1` (`git switch main`, `git merge --ff-only 4d-1`). Ask the user before pushing. After `git push`, wait for both tooling CI jobs (Ubuntu, Windows) to finish, then reset `src/ghost` to `origin/main` once it matches webops.
