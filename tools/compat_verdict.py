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
