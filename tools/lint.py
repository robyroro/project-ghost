#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Repository hygiene checks that do not need a Chromium checkout.

- Every source file we author carries the MPL-2.0 notice (Exhibit A).
- Text files use LF line endings.
- Relative links in Markdown point at files that exist.
- patches/ passes `patches.py check`.

Only files tracked by git are checked, so build output and editor files never
produce noise. Usage: python tools/lint.py
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote

import patches
import repo

MPL_NOTICE = ("This Source Code Form is subject to the terms of the Mozilla Public License, "
              "v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain "
              "one at https://mozilla.org/MPL/2.0/.")
HEADER_EXTENSIONS = {".py", ".gn", ".gni", ".cc", ".h", ".mm", ".rs", ".ts", ".js", ".mojom",
                     ".css", ".html", ".proto", ".grd", ".grdp"}
# Vendored code keeps its own license; patches carry upstream's.
HEADER_EXEMPT_DIRS = ("third_party/", "patches/")
HEADER_SEARCH_LINES = 15
_COMMENT_MARKERS = re.compile(r"^\s*(#|//|/\*+|\*+/?|<!--|-->)|(\*/|-->)\s*$")
_MD_LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")


def tracked_files(root: Path) -> list[str]:
    out = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], capture_output=True,
                         check=True).stdout
    return [p for p in out.decode("utf-8").split("\0") if p]


def has_mpl_notice(text: str) -> bool:
    head = text.splitlines()[:HEADER_SEARCH_LINES]
    stripped = " ".join(_COMMENT_MARKERS.sub("", line).strip() for line in head)
    return MPL_NOTICE in " ".join(stripped.split())


def is_binary(data: bytes) -> bool:
    return b"\0" in data[:8192]


def broken_links(md_path: Path, text: str, root: Path) -> list[str]:
    broken = []
    for target in _MD_LINK.findall(text):
        if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.IGNORECASE) or target.startswith("#"):
            continue  # External URL or in-page anchor.
        path_part = unquote(target.split("#", 1)[0])
        # Code references like docs/foo.md:42 are used in prose; the line suffix is not a path.
        path_part = re.sub(r":\d+$", "", path_part)
        resolved = (md_path.parent / path_part).resolve()
        if not resolved.exists():
            broken.append(target)
        elif root.resolve() not in (resolved, *resolved.parents):
            broken.append(target)
    return broken


def lint(root: Path) -> list[str]:
    problems = []
    for rel in tracked_files(root):
        path = root / rel
        if not path.is_file():
            continue  # Deleted in the working tree but still in the index.
        data = path.read_bytes()
        if is_binary(data) or rel.endswith(".patch"):
            continue  # Patches are validated by patches.check, which allows upstream CRLF files.
        if b"\r\n" in data:
            problems.append(f"{rel}: CRLF line endings (the repo is LF-only; see .gitattributes)")
        text = data.decode("utf-8", "replace")
        if Path(rel).suffix in HEADER_EXTENSIONS and not rel.startswith(HEADER_EXEMPT_DIRS):
            if not has_mpl_notice(text):
                problems.append(f"{rel}: missing the MPL-2.0 notice in the first "
                                f"{HEADER_SEARCH_LINES} lines")
        if rel.endswith(".md"):
            problems += [f"{rel}: broken relative link {t}" for t in broken_links(path, text, root)]
    problems += [f"patches/{i.patch}: {i.message}"
                 for i in patches.check(root / "patches") if i.error]
    return problems


def main() -> int:
    problems = lint(repo.REPO_ROOT)
    for problem in problems:
        print(problem)
    print(f"lint: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
