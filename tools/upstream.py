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
import os
import re
import subprocess
import sys
import urllib.request
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import bootstrap
import patches
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


def find_release(version: str, fetch: Callable) -> Release:
    """VERSION as chromiumdash lists it on Windows, from Extended or Stable."""
    for channel in ("Extended", "Stable"):
        for release in releases(channel, fetch, count=100):
            if release.version == version:
                return release
    raise UpstreamError(f"chromiumdash lists no Windows release {version} on Extended or Stable")


def _git(repo_dir: Path, *args: str, check: bool = True,
         stdin: str | None = None) -> subprocess.CompletedProcess:
    proc = subprocess.run(["git", "-C", str(repo_dir), *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace", input=stdin)
    if check and proc.returncode:
        raise UpstreamError(f"git {' '.join(args)} in {repo_dir} failed:\n{proc.stderr.strip()}")
    return proc


def _move_branch(src: Path, version: str, patches_dir: Path) -> None:
    """Puts the series on ghost/VERSION, cut from VERSION's tag. gclient sync
    rebases the checked-out branch onto the pinned commit, and on a shallow
    checkout the old and new tags share no history: the branch must already
    sit on the new tag. On failure, src is back on the branch it was on."""
    start = _git(src, "rev-parse", "--abbrev-ref", "HEAD").stdout.strip()
    branch = f"ghost/{version}"
    try:
        code = patches.apply(src, f"refs/tags/{version}", branch, patches_dir)
    except patches.PatchError as e:
        raise UpstreamError(str(e)) from None
    if code:
        _git(src, "am", "--abort", check=False)
        _git(src, "checkout", "-q", start)
        _git(src, "branch", "-q", "-D", branch, check=False)
        raise UpstreamError(f"the series didn't apply to {version} although the canary passed; "
                            f"{src} is back on {start}")


# Official builds salt Mojo's message IDs with this byte copy of upstream's
# chrome/VERSION at the pin (build/args/release.gn); release.py checks it.
SALT = Path("build") / "mojom_message_id_salt"


def _write_pin(webops: Path, pinned: str, release: Release, src: Path) -> None:
    salt = subprocess.run(["git", "-C", str(src), "show",
                           f"refs/tags/{release.version}:chrome/VERSION"],
                          capture_output=True, check=True).stdout
    (webops / SALT).write_bytes(salt)
    (webops / "CHROMIUM_VERSION").write_text(release.version + "\n", encoding="utf-8",
                                             newline="\n")
    (webops / "CHROMIUM_COMMIT").write_text(release.commit + "\n", encoding="utf-8",
                                            newline="\n")
    # The toolchain requirements are a milestone's: only the version moves,
    # in chromium_version and in the links to the tag they were read from.
    path = webops / "build" / "requirements.json"
    text = path.read_text(encoding="utf-8").replace(pinned, release.version)
    if json.loads(text).get("chromium_version") != release.version:
        raise UpstreamError(f"{path} doesn't name {pinned} as its chromium_version")
    path.write_text(text, encoding="utf-8", newline="\n")


def bump(version: str, src: Path, webops: Path = repo.REPO_ROOT,
         fetch: Callable | None = None) -> str:
    """Moves the pin to VERSION, a security release of the pin's milestone.
    Every check comes before anything is written."""
    fetch = fetch or fetch_json
    if _git(webops, "status", "--porcelain").stdout.strip():
        raise UpstreamError(f"{webops} has uncommitted changes")
    pinned = repo.read_chromium_version(webops)
    if repo.parse_version(version) <= repo.parse_version(pinned):
        raise UpstreamError(f"{version} is not newer than the pin, {pinned}")
    if milestone_of(version) != milestone_of(pinned):
        raise UpstreamError(f"{version} is milestone {milestone_of(version)}, the pin "
                            f"{milestone_of(pinned)}: a new milestone is docs/patching.md's "
                            "\"A new milestone\", not bump")
    release = find_release(version, fetch)
    tag = f"refs/tags/{version}"
    try:
        bootstrap.record_tag(src, tag, release.commit, dict(os.environ))
    except bootstrap.BootstrapError as e:
        raise UpstreamError(f"the tag doesn't match chromiumdash: {e}") from None

    patches_dir = webops / "patches"
    try:
        results = patches.canary(src, tag, patches_dir)
    except patches.PatchError as e:
        raise UpstreamError(str(e)) from None
    print(patches.format_canary(results, tag))
    if any(r.status in ("conflict", "failed") for r in results):
        raise UpstreamError(f"the series doesn't apply to {version}; resolve it as "
                            "docs/patching.md says, then run bump again. Nothing changed.")

    _move_branch(src, version, patches_dir)
    try:
        patches.export(src, tag, patches_dir)
    except patches.PatchError as e:
        raise UpstreamError(str(e)) from None
    _write_pin(webops, pinned, release, src)
    merged = sum(r.status == "merged" for r in results)
    message = (f"build: move to Chromium {version}\n\n"
               f"A security release of milestone {release.milestone}, published "
               f"{_utc(release.published)}. Its tag is {release.commit}, as chromiumdash "
               f"lists it. The patch series applies to it: {len(results) - merged} clean, "
               f"{merged} with moved context; patches/ re-exported from ghost/{version}.\n")
    _git(webops, "add", "--", "CHROMIUM_VERSION", "CHROMIUM_COMMIT",
         "build/requirements.json", SALT.as_posix(), "patches")
    _git(webops, "commit", "-q", "-F", "-", stdin=message)
    return (f"Moved to Chromium {version}: {src} is on ghost/{version}; committed the pin in "
            f"{webops}. Next: push, wait for CI, tag {version}-1 ({RUNBOOK}).")


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
    b = sub.add_parser("bump")
    b.add_argument("--to", required=True, help="the security release, e.g. 152.0.7977.158")
    b.add_argument("--src", type=Path, required=True, help="the Chromium checkout (src)")
    args = parser.parse_args(argv)
    try:
        if args.command == "check":
            result = check()
            print(json.dumps(result, indent=1) if args.json else _summary(result))
        elif args.command == "report":
            print(report(json.loads(args.check.read_text(encoding="utf-8"))))
        else:
            print(bump(args.to, args.src))
    except UpstreamError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
