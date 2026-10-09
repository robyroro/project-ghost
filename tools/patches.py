#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Manages the upstream patch series in patches/.

patches/ is the exported form of a branch in the Chromium checkout:
  apply   cut the branch from the pinned tag and `git am -3` every patch onto it
  export  regenerate patches/ from the commits between the tag and HEAD
  check   validate patch files (naming, required trailers, scope)
  stats   measure our divergence from upstream
  canary  report which patches would conflict on another upstream revision,
          without touching the checkout

Edits happen with ordinary git in the Chromium checkout (commit, rebase -i,
fixup); `export` turns the result back into files for review. Moving to a new
Chromium tag is `git rebase --onto <new-tag> <old-tag>` followed by `export`,
which resolves conflicts with real three-way merges instead of fuzzy patching.
See docs/patching.md.
"""

from __future__ import annotations

import argparse
import email.header
import email.parser
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import repo

PATCHES_DIR = repo.REPO_ROOT / "patches"
PATCH_NAME_RE = re.compile(r"^(\d{4})-[A-Za-z0-9._-]+\.patch$")
ZERO_FROM_RE = re.compile(rb"^From 0{40} Mon Sep 17 00:00:00 2001$")
TRAILER_RE = re.compile(r"^(Why|Upstream):[ \t]*(\S.*)$", re.MULTILINE)
DIFF_HEADER_RE = re.compile(r"^diff --git a/(.+?) b/(.+)$")
REQUIRED_TRAILERS = ("Why", "Upstream")

# Changes here get a second reviewer (CONTRIBUTING.md): they sit on security
# boundaries or on the network path every request takes.
SENSITIVE_PREFIXES = (
    "chrome/updater/", "components/update_client/", "content/browser/renderer_host/",
    "crypto/", "mojo/", "net/", "sandbox/", "services/network/", "third_party/boringssl/",
)
LARGE_PATCH_LINES = 50

# format-patch output depends on user config (diff.noprefix breaks `git am`,
# diff.algorithm changes hunks, format.* adds headers). Pin everything that
# affects the bytes so every developer exports identical files.
_GIT_CONFIG_OVERRIDES = (
    "-c", "diff.noprefix=false", "-c", "diff.mnemonicPrefix=false",
    "-c", "diff.interHunkContext=0", "-c", "diff.suppressBlankEmpty=false",
    "-c", "diff.relative=false", "-c", "core.quotePath=true",
    "-c", "format.coverLetter=false", "-c", "format.signOff=false",
    "-c", "format.encodeEmailHeaders=true",
)
_FORMAT_PATCH_FLAGS = (
    "--zero-commit", "--no-signature", "--no-stat", "--no-numbered", "--full-index",
    "--no-renames", "--unified=3", "--diff-algorithm=myers", "--subject-prefix=PATCH",
    "--no-thread", "--no-to", "--no-cc", "--no-notes", "--filename-max-length=64",
)


class PatchError(Exception):
    pass


def _git(src: Path, *args: str, check: bool = True,
         env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    proc = subprocess.run(["git", "-C", str(src), *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace", env=env)
    if check and proc.returncode != 0:
        raise PatchError(f"git {' '.join(args)} failed:\n{proc.stderr.strip()}")
    return proc


def list_patches(patches_dir: Path) -> list[Path]:
    return sorted(patches_dir.glob("*.patch"))


def _require_commit(src: Path, rev: str) -> None:
    if _git(src, "rev-parse", "--verify", "--quiet", f"{rev}^{{commit}}", check=False).returncode:
        raise PatchError(
            f"{rev} is not available in {src}. For a shallow checkout fetch it with: "
            f"git -C {src} fetch --depth=1 origin tag {rev.removeprefix('refs/tags/')}")


def apply(src: Path, base: str, branch: str, patches_dir: Path, force: bool = False) -> int:
    _require_commit(src, base)
    if _git(src, "status", "--porcelain", "--untracked-files=no").stdout.strip():
        raise PatchError(f"{src} has uncommitted changes; commit or stash them first")
    exists = _git(src, "rev-parse", "--verify", "--quiet", f"refs/heads/{branch}",
                  check=False).returncode == 0
    if exists and not force:
        raise PatchError(f"branch {branch} already exists; --force recreates it and discards "
                         "any commits on it that were not exported")
    _git(src, "checkout", "-q", "-B" if force else "-b", branch, base)
    patches = list_patches(patches_dir)
    if not patches:
        print(f"No patches in {patches_dir}; {branch} is at {base}.")
        return 0
    # --keep-cr: some upstream files are CRLF and their context lines must
    # survive mail splitting byte for byte.
    proc = _git(src, "am", "-3", "--keep-cr", "--quiet", "--whitespace=nowarn", "--",
                *(str(p.resolve()) for p in patches), check=False)
    if proc.returncode:
        print(proc.stdout + proc.stderr, file=sys.stderr)
        print(f"error: a patch did not apply. Resolve it in {src}, then "
              "`git am --continue` (or --abort), and run `patches.py export`.", file=sys.stderr)
        return 1
    print(f"Applied {len(patches)} patch(es) onto {base} as {branch}.")
    return 0


def render_series(src: Path, base: str) -> dict[str, bytes]:
    """The series as `export` would write it, file name -> bytes; writes nothing."""
    _require_commit(src, base)
    rng = f"{base}..HEAD"
    if _git(src, "rev-list", "--merges", rng).stdout.strip():
        raise PatchError(f"{rng} contains merge commits; the series must be linear")
    with tempfile.TemporaryDirectory() as tmp:
        _git(src, *_GIT_CONFIG_OVERRIDES, "format-patch", *_FORMAT_PATCH_FLAGS,
             "-o", tmp, rng)
        return {p.name: p.read_bytes() for p in sorted(Path(tmp).glob("*.patch"))}


def series_matches(src: Path, base: str, patches_dir: Path) -> bool:
    """Whether the branch checked out in src is exactly the series in patches_dir."""
    return render_series(src, base) == {p.name: p.read_bytes()
                                        for p in list_patches(patches_dir)}


def export(src: Path, base: str, patches_dir: Path) -> int:
    new = render_series(src, base)
    old = {p.name: p.read_bytes() for p in list_patches(patches_dir)}
    patches_dir.mkdir(parents=True, exist_ok=True)
    for name in old:
        (patches_dir / name).unlink()
    for name, data in new.items():
        (patches_dir / name).write_bytes(data)
    changed = sum(1 for n in new if n in old and new[n] != old[n])
    print(f"Exported {len(new)} patch(es) to {patches_dir}: "
          f"{len(new.keys() - old.keys())} added, {len(old.keys() - new.keys())} removed, "
          f"{changed} changed.")
    return 0


@dataclass(frozen=True)
class CanaryResult:
    patch: str
    # "clean": applies as it is. "merged": its context moved, and a three-way
    # merge applies it. "conflict": the merge conflicts in `paths`.
    # "failed": it can't be applied at all (`detail` says why).
    status: str
    paths: list[str]
    detail: str = ""


def canary(src: Path, onto: str, patches_dir: Path) -> list[CanaryResult]:
    """Applies the series onto `onto` in a scratch index, patch by patch.

    Nothing in the checkout changes: no files, no HEAD, no branches. A real
    rebase would rewrite every patched file, and the build tool rebuilds what
    it sees modified. Each patch is applied on top of the ones before it that
    applied; a patch that doesn't apply is reported and left out.
    """
    _require_commit(src, onto)
    results = []
    with tempfile.TemporaryDirectory(prefix="canary-") as tmp:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(tmp) / "index"))
        tree = _git(src, "rev-parse", f"{onto}^{{tree}}").stdout.strip()
        for patch in list_patches(patches_dir):
            _git(src, "read-tree", tree, env=env)
            path = str(patch.resolve())
            if _git(src, "apply", "--cached", "--check", path, check=False, env=env).returncode == 0:
                _git(src, "apply", "--cached", path, env=env)
                status, paths, detail = "clean", [], ""
            else:
                merge = _git(src, "apply", "--cached", "--3way", path, check=False, env=env)
                unmerged = sorted({line.split("\t", 1)[1] for line in
                                   _git(src, "ls-files", "-u", env=env).stdout.splitlines()})
                if merge.returncode == 0:
                    status, paths, detail = "merged", [], ""
                elif unmerged:
                    status, paths, detail = "conflict", unmerged, ""
                else:
                    status, paths, detail = "failed", [], merge.stderr.strip()
            if status in ("clean", "merged"):
                tree = _git(src, "write-tree", env=env).stdout.strip()
            results.append(CanaryResult(patch.name, status, paths, detail))
    return results


def format_canary(results: list[CanaryResult], onto: str) -> str:
    lines = []
    for r in results:
        if r.status == "conflict":
            note = "CONFLICT  " + ", ".join(r.paths)
        elif r.status == "failed":
            note = "FAILED    " + r.detail.splitlines()[0] if r.detail else "FAILED"
        else:
            note = r.status
        lines.append(f"{r.patch:<66} {note}")
    work = sum(r.status in ("conflict", "failed") for r in results)
    lines.append(f"{work} of {len(results)} patch(es) need work on {onto}; "
                 f"{sum(r.status == 'merged' for r in results)} apply with moved context.")
    return "\n".join(lines)


@dataclass(frozen=True)
class Issue:
    patch: str
    message: str
    error: bool = True


@dataclass
class ParsedPatch:
    name: str
    subject: str
    message: str
    mail: bytes
    diff: bytes
    paths: list[str]


def parse_patch(path: Path) -> ParsedPatch:
    data = path.read_bytes()
    start = data.find(b"\ndiff --git ")
    mail, diff = (data, b"") if start < 0 else (data[:start + 1], data[start + 1:])
    headers = email.parser.BytesHeaderParser().parsebytes(mail)
    raw_subject = headers.get("Subject", "")
    subject = str(email.header.make_header(email.header.decode_header(raw_subject)))
    subject = " ".join(subject.split())
    body = mail.split(b"\n\n", 1)[1] if b"\n\n" in mail else b""
    message = body.decode("utf-8", "replace").split("\n---\n", 1)[0]
    paths = []
    for line in diff.decode("utf-8", "replace").splitlines():
        m = DIFF_HEADER_RE.match(line)
        if m:
            paths += [m.group(1), m.group(2)]
    return ParsedPatch(path.name, subject, message, mail, diff, sorted(set(paths)))


def check(patches_dir: Path) -> list[Issue]:
    issues = []
    patches = list_patches(patches_dir)
    for index, path in enumerate(patches, 1):
        name = path.name
        m = PATCH_NAME_RE.match(name)
        if not m:
            issues.append(Issue(name, "file name must be NNNN-slug.patch (use patches.py export)"))
        elif int(m.group(1)) != index:
            issues.append(Issue(name, f"expected sequence number {index:04d}; numbering must be "
                                      "contiguous from 0001"))
        p = parse_patch(path)
        if not ZERO_FROM_RE.match(p.mail.split(b"\n", 1)[0]):
            issues.append(Issue(name, "missing zero-hash From line; regenerate with patches.py export"))
        if b"\r" in p.mail:
            issues.append(Issue(name, "commit message or headers contain CR characters"))
        if "﻿" in p.subject or "﻿" in p.message:
            issues.append(Issue(name, "commit message contains a byte-order mark; "
                                      "rewrite it without one"))
        if not p.subject.startswith("[PATCH] ") or not p.subject[8:].strip():
            issues.append(Issue(name, "subject must be '[PATCH] area: summary'"))
        found = {k for k, _ in TRAILER_RE.findall(p.message)}
        for trailer in REQUIRED_TRAILERS:
            if trailer not in found:
                issues.append(Issue(name, f"commit message lacks a '{trailer}:' trailer"))
        if not p.diff:
            issues.append(Issue(name, "contains no diff"))
        if b"\nGIT binary patch" in p.diff or b"\nBinary files " in p.diff:
            issues.append(Issue(name, "binary changes are not allowed in patches; "
                                      "put assets under //ghost and reference them"))
        for touched in p.paths:
            if touched.startswith("ghost/"):
                issues.append(Issue(name, f"touches {touched}; //ghost code belongs in this "
                                          "repository, not in an upstream patch"))
        if b"\r" in p.diff:
            issues.append(Issue(name, "modifies a file with CR line endings; verify the result "
                                      "after `apply`", error=False))
    return issues


@dataclass(frozen=True)
class PatchStat:
    name: str
    files: int
    added: int
    removed: int
    sensitive: bool

    @property
    def needs_second_reviewer(self) -> bool:
        return self.sensitive or self.added + self.removed > LARGE_PATCH_LINES


def count_changes(diff: bytes) -> tuple[int, int]:
    """Counts added/removed lines inside hunks only.

    Counting every line that starts with '+' or '-' would misread file headers
    and removed lines that themselves begin with '--'.
    """
    added = removed = 0
    in_hunk = False
    for line in diff.split(b"\n"):
        if line.startswith(b"diff --git "):
            in_hunk = False
        elif line.startswith(b"@@"):
            in_hunk = True
        elif in_hunk and line.startswith(b"+"):
            added += 1
        elif in_hunk and line.startswith(b"-"):
            removed += 1
    return added, removed


def stats(patches_dir: Path) -> list[PatchStat]:
    result = []
    for path in list_patches(patches_dir):
        p = parse_patch(path)
        added, removed = count_changes(p.diff)
        result.append(PatchStat(p.name, len(p.paths), added, removed,
                                any(t.startswith(SENSITIVE_PREFIXES) for t in p.paths)))
    return result


def format_stats(rows: list[PatchStat]) -> str:
    lines = [f"{'patch':<66} {'files':>5} {'+':>6} {'-':>6}  review"]
    for r in rows:
        flags = []
        if r.sensitive:
            flags.append("sensitive")
        if r.added + r.removed > LARGE_PATCH_LINES:
            flags.append("large")
        lines.append(f"{r.name:<66} {r.files:>5} {r.added:>6} {r.removed:>6}  "
                     f"{'2 reviewers: ' + ', '.join(flags) if flags else '-'}")
    lines.append(f"{len(rows)} patch(es), +{sum(r.added for r in rows)} "
                 f"-{sum(r.removed for r in rows)} lines, "
                 f"{sum(r.needs_second_reviewer for r in rows)} need a second reviewer")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    version = repo.read_chromium_version()
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--patches-dir", type=Path, default=PATCHES_DIR)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("apply", "export"):
        p = sub.add_parser(name)
        p.add_argument("--src", type=Path, required=True, help="Chromium checkout (the src dir)")
        p.add_argument("--base", default=f"refs/tags/{version}",
                       help="upstream revision the series sits on (default: pinned tag)")
        if name == "apply":
            p.add_argument("--branch", default=f"ghost/{version}")
            p.add_argument("--force", action="store_true",
                           help="recreate the branch if it already exists")
    sub.add_parser("check")
    sub.add_parser("stats")
    c = sub.add_parser("canary")
    c.add_argument("--src", type=Path, required=True, help="Chromium checkout (the src dir)")
    c.add_argument("--onto", required=True,
                   help="upstream revision to try the series on, e.g. refs/tags/<version>")
    args = parser.parse_args(argv)

    try:
        if args.command == "apply":
            return apply(args.src, args.base, args.branch, args.patches_dir, args.force)
        if args.command == "export":
            return export(args.src, args.base, args.patches_dir)
        if args.command == "canary":
            results = canary(args.src, args.onto, args.patches_dir)
            print(format_canary(results, args.onto))
            return 1 if any(r.status in ("conflict", "failed") for r in results) else 0
    except PatchError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    if args.command == "check":
        issues = check(args.patches_dir)
        for issue in issues:
            print(f"{'error' if issue.error else 'warning'}: {issue.patch}: {issue.message}")
        errors = sum(i.error for i in issues)
        print(f"{len(list_patches(args.patches_dir))} patch(es) checked, {errors} error(s).")
        return 1 if errors else 0
    print(format_stats(stats(args.patches_dir)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
