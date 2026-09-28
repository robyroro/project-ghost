# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Access to repository-level pins shared by the tools.

CHROMIUM_VERSION is the single source of truth for the upstream tag. The
toolchain requirements in build/requirements.json are only valid for that tag,
so loading them verifies the two agree; a version bump that forgets to update
the requirements fails here instead of producing a confusing build error.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

_CHROMIUM_VERSION_RE = re.compile(r"^\d+\.\d+\.\d+\.\d+$")


class RepoError(Exception):
    pass


def parse_version(text: str) -> tuple[int, ...]:
    """Parses a dotted numeric version such as '10.0.26100.7705'."""
    parts = text.strip().split(".")
    if not parts or not all(p.isdigit() for p in parts):
        raise ValueError(f"not a dotted numeric version: {text!r}")
    return tuple(int(p) for p in parts)


def version_at_least(actual: str, minimum: str) -> bool:
    a, m = parse_version(actual), parse_version(minimum)
    width = max(len(a), len(m))
    return a + (0,) * (width - len(a)) >= m + (0,) * (width - len(m))


def read_chromium_version(root: Path = REPO_ROOT) -> str:
    path = root / "CHROMIUM_VERSION"
    try:
        version = path.read_text(encoding="utf-8").strip()
    except FileNotFoundError:
        raise RepoError(f"{path} is missing") from None
    if not _CHROMIUM_VERSION_RE.match(version):
        raise RepoError(f"{path} must contain a four-part version, got {version!r}")
    return version


def load_requirements(root: Path = REPO_ROOT) -> dict:
    path = root / "build" / "requirements.json"
    with path.open(encoding="utf-8") as f:
        requirements = json.load(f)
    pinned = read_chromium_version(root)
    if requirements.get("chromium_version") != pinned:
        raise RepoError(
            f"{path} describes Chromium {requirements.get('chromium_version')}, "
            f"but CHROMIUM_VERSION is {pinned}; re-derive the requirements "
            "from the new tag's docs/windows_build_instructions.md")
    return requirements
