#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The filter lists the browser ships, committed in components/blocking/data/.

  update   fetch EasyList and EasyPrivacy, write them and VERSIONS.json
  check    verify each list against VERSIONS.json (the tooling tests run it)

The lists are data: shipped unmodified beside the browser, never compiled
into it (docs/licensing.md). Committing them keeps builds reproducible and
makes each refresh a diff that can be reviewed.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import re
import sys
import urllib.request
from collections.abc import Callable
from pathlib import Path

import repo

DATA = repo.REPO_ROOT / "components" / "blocking" / "data"
VERSIONS = "VERSIONS.json"
LISTS = {
    "easylist.txt": "https://easylist.to/easylist/easylist.txt",
    "easyprivacy.txt": "https://easylist.to/easylist/easyprivacy.txt",
}


class ListError(Exception):
    pass


def fetch_url(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "Shade-build"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def _version(text: str) -> str:
    m = re.search(r"^! Version: *(\S+)", text, re.MULTILINE)
    return m.group(1) if m else ""


def update(data: Path, fetch: Callable[[str], bytes] = fetch_url,
           now: datetime.datetime | None = None) -> dict:
    """Fetches every list first and writes only when all are good."""
    fetched = {}
    for name, url in LISTS.items():
        raw = fetch(url)
        text = raw.decode("utf-8")
        if not text.startswith("[Adblock Plus") or "! Title:" not in text:
            raise ListError(f"{url} is not a filter list (it begins {text[:40]!r})")
        fetched[name] = raw
    day = (now or datetime.datetime.now(datetime.timezone.utc)).strftime("%Y-%m-%d")
    versions = {}
    data.mkdir(parents=True, exist_ok=True)
    for name, raw in fetched.items():
        (data / name).write_bytes(raw)
        versions[name] = {"url": LISTS[name], "fetched": day,
                          "version": _version(raw.decode("utf-8")),
                          "sha256": hashlib.sha256(raw).hexdigest()}
    (data / VERSIONS).write_text(json.dumps(versions, indent=1, sort_keys=True) + "\n",
                                 encoding="utf-8", newline="\n")
    return versions


def check(data: Path) -> list[str]:
    record = data / VERSIONS
    if not record.exists():
        return [f"{record} is missing"]
    versions = json.loads(record.read_text(encoding="utf-8"))
    problems = []
    for name in sorted(LISTS):
        path = data / name
        if not path.exists():
            problems.append(f"{name} is missing; run tools/filter_lists.py update")
        elif hashlib.sha256(path.read_bytes()).hexdigest() != versions.get(name, {}).get("sha256"):
            problems.append(f"{name} differs from {VERSIONS}; run tools/filter_lists.py update")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=("update", "check"))
    args = parser.parse_args(argv)
    if args.command == "update":
        try:
            for name, record in update(DATA).items():
                print(f"{name}: version {record['version']}, sha256 {record['sha256'][:16]}")
        except (ListError, OSError) as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        return 0
    problems = check(DATA)
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
