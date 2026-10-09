#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Follows Chromium's Extended Stable channel: notices its releases and moves the pin to them.

  check [--json]           compare Extended Stable's latest Windows release
                           with CHROMIUM_VERSION
  report --check FILE      open the GitHub issue a check calls for, unless one
                           with its title exists (the scheduled workflow,
                           .github/workflows/upstream.yml)
  bump --to VERSION --src SRC
                           move to a security release of the same milestone:
                           verify its tag, try the patch series on it, put the
                           series on a branch cut from it, commit the new pin
                           and the re-exported series

The process around these is docs/build/security-release.md.
"""

from __future__ import annotations

import argparse
import datetime
import json
import re
import subprocess
import sys
import urllib.request
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import repo

DASH = "https://chromiumdash.appspot.com/fetch_releases"
SLA = datetime.timedelta(hours=72)
REPOSITORY = "https://github.com/robyroro/project-ghost"
RUNBOOK = "docs/build/security-release.md"
# GitHub label colours.
LABELS = {"security-release": "d73a4a", "milestone": "0e8a16"}


class UpstreamError(Exception):
    pass


@dataclass(frozen=True)
class Release:
    version: str
    milestone: int
    commit: str
    published: datetime.datetime  # UTC


def fetch_json(url: str):
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            return json.load(response)
    except (OSError, ValueError) as e:
        raise UpstreamError(f"{url}: {e}") from None


def releases(channel: str, fetch: Callable, count: int = 1) -> list[Release]:
    """The channel's latest Windows releases, newest first."""
    url = f"{DASH}?channel={channel}&platform=Windows&num={count}"
    data = fetch(url)
    if not isinstance(data, list):
        raise UpstreamError(f"{url}: expected a list of releases, got {type(data).__name__}")
    return [_release(entry, url) for entry in data]


def _release(entry, url: str) -> Release:
    try:
        version, commit = entry["version"], entry["hashes"]["chromium"]
        repo.parse_version(version)
        published = datetime.datetime.fromtimestamp(entry["time"] / 1000, datetime.timezone.utc)
        milestone = int(entry["milestone"])
    except (KeyError, TypeError, ValueError) as e:
        raise UpstreamError(f"{url}: a release without a usable {e}") from None
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise UpstreamError(f"{url}: {version}'s chromium commit is {commit!r}")
    return Release(version, milestone, commit, published)


def latest_extended(fetch: Callable) -> Release:
    found = releases("Extended", fetch)
    if not found:
        raise UpstreamError("chromiumdash lists no Extended Stable release for Windows")
    return found[0]


def milestone_of(version: str) -> int:
    return repo.parse_version(version)[0]


def verdict(pinned: str, latest: Release) -> str:
    """current, security-release (the pin's milestone, newer) or milestone (a newer one)."""
    if repo.parse_version(latest.version) <= repo.parse_version(pinned):
        return "current"
    return "security-release" if latest.milestone == milestone_of(pinned) else "milestone"


def _utc(moment: datetime.datetime) -> str:
    return moment.astimezone(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")


def issue(latest: Release, kind: str, detected: datetime.datetime) -> dict:
    """The GitHub issue a check calls for: title, labels, body."""
    if kind == "security-release":
        title = f"Security release: Chromium {latest.version}"
        steps = [
            "- [ ] Triage: does the release fix a bug exploited in the wild? Then the "
            "deadline is 24–48 h.",
            f"- [ ] `python tools/upstream.py bump --to {latest.version} --src <src>`, push",
            "- [ ] Tooling CI green",
            f"- [ ] Tag `{latest.version}-1`, push it",
            f"- [ ] `python tools/release.py run --tag {latest.version}-1 --src <src>`",
            "- [ ] The publisher key's PIN at `sign`",
            f"- [ ] `python tools/release.py verify --tag {latest.version}-1`",
        ]
    else:
        title = f"Milestone: Chromium {latest.milestone} on Extended"
        steps = [
            f"- [ ] Move to milestone {latest.milestone}: [patching.md, A new milestone]"
            f"({REPOSITORY}/blob/main/docs/patching.md#a-new-milestone)",
        ]
    body = "\n".join([
        f"Chromium **{latest.version}** is Extended Stable's latest Windows release, "
        f"commit `{latest.commit}`.",
        "",
        f"- Published: {_utc(latest.published)}",
        f"- Detected: {_utc(detected)}",
        f"- Deadline: {_utc(latest.published + SLA)}, publication + 72 h "
        f"([SLA]({REPOSITORY}/blob/main/docs/roadmap.md#security-release-sla))",
        f"- Release notes: https://chromereleases.googleblog.com/search?q={latest.version}",
        "",
        f"Follow [the runbook]({REPOSITORY}/blob/main/{RUNBOOK}), and note each step's time "
        "here as a comment.",
        "",
        *steps,
    ])
    return {"title": title, "labels": [kind], "body": body}


def check(webops: Path = repo.REPO_ROOT, fetch: Callable | None = None,
          now: datetime.datetime | None = None) -> dict:
    pinned = repo.read_chromium_version(webops)
    latest = latest_extended(fetch or fetch_json)
    kind = verdict(pinned, latest)
    result = {"verdict": kind, "pinned": pinned, "version": latest.version,
              "milestone": latest.milestone, "commit": latest.commit,
              "published": latest.published.isoformat()}
    if kind != "current":
        result["issue"] = issue(latest, kind, now or datetime.datetime.now(datetime.timezone.utc))
    return result


def gh(args: list[str]) -> str:
    return subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8",
                          check=True).stdout


def report(result: dict, run: Callable[[list[str]], str] = gh) -> str:
    """Opens the issue `result` calls for, unless an issue with its title exists,
    open or closed. Returns what it did."""
    if result["verdict"] == "current":
        return f"current: the pin is {result['pinned']}, Extended Stable's latest"
    wanted = result["issue"]
    found = json.loads(run(["issue", "list", "--state", "all", "--limit", "100",
                            "--search", f'"{wanted["title"]}" in:title',
                            "--json", "title,url"]) or "[]")
    same = [i for i in found if i["title"] == wanted["title"]]
    if same:
        return f"already reported: {same[0]['url']}"
    for label in wanted["labels"]:
        run(["label", "create", label, "--color", LABELS[label], "--force"])
    url = run(["issue", "create", "--title", wanted["title"], "--body", wanted["body"],
               "--label", ",".join(wanted["labels"])]).strip()
    return f"opened {url}"


def _summary(result: dict) -> str:
    return (f"{result['verdict']}: Extended Stable is {result['version']} (milestone "
            f"{result['milestone']}, published {result['published']}); the pin is "
            f"{result['pinned']}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    c = sub.add_parser("check")
    c.add_argument("--json", action="store_true", help="print the result as JSON")
    r = sub.add_parser("report")
    r.add_argument("--check", type=Path, required=True, help="check --json's output")
    args = parser.parse_args(argv)
    try:
        if args.command == "check":
            result = check()
            print(json.dumps(result, indent=1) if args.json else _summary(result))
        else:
            print(report(json.loads(args.check.read_text(encoding="utf-8"))))
    except UpstreamError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
