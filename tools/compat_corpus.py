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
