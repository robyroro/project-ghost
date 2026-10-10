#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Repository hygiene checks that do not need a Chromium checkout.

- Every source file we author carries the MPL-2.0 notice (Exhibit A).
- Text files use LF line endings.
- Relative links in Markdown point at files that exist.
- patches/ passes `patches.py check`.
- No private key outside test/updater/, where the committed test keys live.

Only files tracked by git are checked, so build output and editor files never
produce noise. Usage: python tools/lint.py
"""

from __future__ import annotations

import json
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
_PRIVATE_KEY_PEM = re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")
PRIVATE_KEYS_ALLOWED = ("test/updater/",)


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


def strip_code_fences(text: str) -> str:
    """Drops fenced code blocks, whose '#' lines and brackets are not Markdown."""
    kept, fence = [], None
    for line in text.splitlines():
        marker = re.match(r"^\s*(`{3,}|~{3,})", line)
        if fence is None and marker:
            fence = marker.group(1)[0] * 3
        elif fence is not None and line.strip().startswith(fence):
            fence = None
        elif fence is None:
            kept.append(line)
    return "\n".join(kept)


def github_anchors(markdown: str) -> set[str]:
    """Anchors GitHub generates for headings, including -N suffixes for duplicates."""
    anchors: set[str] = set()
    seen: dict[str, int] = {}
    for m in re.finditer(r"^#{1,6}\s+(.+?)\s*#*\s*$", strip_code_fences(markdown), re.MULTILINE):
        text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", m.group(1)).replace("`", "")
        slug = re.sub(r"[^\w\- ]", "", text.strip().lower()).replace(" ", "-")
        count = seen.get(slug, 0)
        seen[slug] = count + 1
        anchors.add(slug if count == 0 else f"{slug}-{count}")
    return anchors


def broken_links(md_path: Path, text: str, root: Path) -> list[str]:
    broken = []
    for target in _MD_LINK.findall(strip_code_fences(text)):
        if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.IGNORECASE):
            continue  # External URL.
        path_part, _, fragment = target.partition("#")
        path_part = unquote(path_part)
        # Code references like docs/foo.md:42 are used in prose; the line suffix is not a path.
        path_part = re.sub(r":\d+$", "", path_part)
        resolved = (md_path.parent / path_part).resolve() if path_part else md_path.resolve()
        if not resolved.exists() or root.resolve() not in (resolved, *resolved.parents):
            broken.append(target)
        elif fragment and resolved.suffix == ".md":
            if fragment not in github_anchors(resolved.read_text(encoding="utf-8")):
                broken.append(target)
    return broken


def _has_private_key_field(value) -> bool:
    if isinstance(value, dict):
        return "private_key" in value or any(_has_private_key_field(v) for v in value.values())
    if isinstance(value, list):
        return any(_has_private_key_field(v) for v in value)
    return False


def private_key_problem(rel: str, text: str) -> str | None:
    """A private key outside test/updater/ would be a custody key committed by mistake."""
    if rel.startswith(PRIVATE_KEYS_ALLOWED):
        return None
    found = bool(_PRIVATE_KEY_PEM.search(text))
    if not found and rel.endswith(".json"):
        try:
            found = _has_private_key_field(json.loads(text))
        except ValueError:
            found = False
    return f"{rel}: a private key outside test/updater/" if found else None


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
        problem = private_key_problem(rel, text)
        if problem:
            problems.append(problem)
        if Path(rel).suffix in HEADER_EXTENSIONS and not rel.startswith(HEADER_EXEMPT_DIRS):
            if not has_mpl_notice(text):
                problems.append(f"{rel}: missing the MPL-2.0 notice in the first "
                                f"{HEADER_SEARCH_LINES} lines")
        # Vendored code keeps its own documentation, links and all.
        if rel.endswith(".md") and not rel.startswith(HEADER_EXEMPT_DIRS):
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
