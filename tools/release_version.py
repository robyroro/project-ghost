#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Release versions (ADR 0007): from a release tag to chrome/VERSION.

Release tags are CHROMIUM_VERSION, optionally followed by -<respin>:
152.0.7977.149 is respin 0, 152.0.7977.149-1 is respin 1. The release version
keeps Chromium's MAJOR.MINOR.BUILD and sets the fourth part to
PATCH * 100 + respin, so each release sorts above the one before it.

  compute --tag T          print T's release version
  write --src S --tag T    write it into S/chrome/VERSION (release builds only)

Only the release pipeline writes. Development builds keep upstream's
chrome/VERSION, which then equals CHROMIUM_VERSION.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import repo

FIELD_MAX = 65535  # Windows version resources hold 16 bits per part.
RESPIN_LIMIT = 100

_TAG_RE = re.compile(r"^(\d+\.\d+\.\d+\.\d+)(?:-([1-9]\d*))?$")
_KEYS = ("MAJOR", "MINOR", "BUILD", "PATCH")


class ReleaseVersionError(Exception):
    pass


@dataclass(frozen=True)
class Release:
    chromium: tuple[int, int, int, int]
    respin: int

    @property
    def patch(self) -> int:
        return self.chromium[3] * 100 + self.respin

    @property
    def version(self) -> str:
        major, minor, build, _ = self.chromium
        return f"{major}.{minor}.{build}.{self.patch}"


def parse_tag(tag: str, chromium_version: str) -> Release:
    m = _TAG_RE.match(tag)
    if not m:
        raise ReleaseVersionError(f"release tag {tag!r} is not <chromium version>[-<respin>]; "
                                  "respin 0 has no suffix")
    if m.group(1) != chromium_version:
        raise ReleaseVersionError(f"release tag {tag!r} is for Chromium {m.group(1)}, "
                                  f"but CHROMIUM_VERSION is {chromium_version}")
    major, minor, build, patch = (int(p) for p in chromium_version.split("."))
    respin = int(m.group(2) or 0)
    if respin >= RESPIN_LIMIT:
        raise ReleaseVersionError(f"respin {respin} does not fit in the two digits "
                                  "ADR 0007 gives it")
    if patch * 100 + respin > FIELD_MAX:
        raise ReleaseVersionError(f"{patch} * 100 + {respin} exceeds {FIELD_MAX}, the largest "
                                  "Windows version field; ADR 0007's scheme has to change")
    return Release((major, minor, build, patch), respin)


def write_version_file(src: Path, release: Release) -> None:
    path = src / "chrome" / "VERSION"
    text = path.read_text(encoding="utf-8")
    values = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    current = ".".join(values.get(k, "?") for k in _KEYS)
    chromium = ".".join(str(p) for p in release.chromium)
    if current != chromium:
        raise ReleaseVersionError(f"{path} is {current}, not Chromium {chromium}: it was "
                                  "already written, or the checkout is at another pin")
    path.write_text(re.sub(r"(?m)^PATCH=\d+$", f"PATCH={release.patch}", text),
                    encoding="utf-8", newline="\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    compute = sub.add_parser("compute", help="print the release version")
    compute.add_argument("--tag", required=True)
    write = sub.add_parser("write", help="write the release version into chrome/VERSION")
    write.add_argument("--src", type=Path, required=True, help="Chromium checkout (the src dir)")
    write.add_argument("--tag", required=True)
    args = parser.parse_args(argv)
    try:
        release = parse_tag(args.tag, repo.read_chromium_version())
        if args.command == "write":
            write_version_file(args.src, release)
    except (ReleaseVersionError, repo.RepoError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(release.version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
