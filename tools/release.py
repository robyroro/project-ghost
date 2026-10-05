#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Makes a release from a release tag, on the machine that holds the keys (sub-project E).

  run --tag T --src SRC [--identity test] [--update-url URL] [--redo STAGE]
      [--stage FRACTION --host USER@HOST] [--public] [--releases DIR]
        checks the repositories, then runs the stages: sync, apply, build,
        test, sign, describe, draft, stage. Each records its inputs and
        outputs in <releases>/<tag>/state.json; a re-run skips the stages
        whose inputs are unchanged.
  verify DIR [--identity test]
        every hash, the provenance, the Authenticode signature, the CRX3 proof
  rollout --fraction F | halt | promote | drop   --host USER@HOST [--appid A]
        the update server's candidate release, through ghost-update-admin

docs/build/release.md is the runbook.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import release_version
import repo

OUT = Path("out") / "release"
DEFAULT_RELEASES = Path.home() / "ProjectGhostReleases"
DEFAULT_DEPOT_TOOLS = Path(r"C:\src\depot_tools")
DEFAULT_SERVER_REPO = repo.REPO_ROOT.parent / "project-ghost-update-server"
# Identities that never make a public release: the development keys are
# committed, and the test identity's name isn't cleared (docs/licensing.md).
NOT_PUBLIC = ("dev", "test")


class ReleaseError(Exception):
    pass


def render_args(identity: str, update_url: str | None) -> str:
    lines = ["# Written by tools/release.py; edit build/args/release.gn instead.",
             'import("//ghost/build/args/release.gn")',
             f'ghost_signing_identity = "{identity}"']
    if update_url:
        lines.append(f'ghost_update_url = "{update_url}"')
    return "\n".join(lines) + "\n"


def browser_appid(root: Path = repo.REPO_ROOT) -> str:
    text = (root / "branding" / "updater.gni").read_text(encoding="utf-8")
    m = re.search(r'^\s*browser_appid\s*=\s*"(\{[0-9a-fA-F-]{36}\})"', text, re.M)
    if not m:
        raise ReleaseError("branding/updater.gni has no browser_appid")
    return m.group(1)


@dataclass(frozen=True)
class Context:
    tag: str
    src: Path
    identity: str = "test"
    update_url: str | None = None
    webops: Path = repo.REPO_ROOT
    depot_tools: Path = DEFAULT_DEPOT_TOOLS
    releases: Path = DEFAULT_RELEASES
    server_repo: Path = DEFAULT_SERVER_REPO
    host: str | None = None
    fraction: float | None = None
    public: bool = False
    jobs: int = 10
    python: str = sys.executable

    @property
    def root(self) -> Path:
        return self.src.parent

    @property
    def out(self) -> Path:
        return self.src / OUT

    @property
    def dir(self) -> Path:
        return self.releases / self.tag

    @property
    def chromium_version(self) -> str:
        return repo.read_chromium_version(self.webops)

    @property
    def version(self) -> str:
        return release_version.parse_tag(self.tag, self.chromium_version).version

    @property
    def base(self) -> str:
        return f"refs/tags/{self.chromium_version}"


def _git(directory: Path, *args: str, check: bool = True) -> str:
    proc = subprocess.run(["git", "-C", str(directory), *args], capture_output=True,
                          text=True, encoding="utf-8", errors="replace")
    if check and proc.returncode:
        raise ReleaseError(f"git {' '.join(args)} in {directory} failed: {proc.stderr.strip()}")
    return proc.stdout.strip() if proc.returncode == 0 else ""


def tag_commit(ctx: Context) -> str:
    return _git(ctx.webops, "rev-parse", "--verify", "--quiet",
                f"refs/tags/{ctx.tag}^{{commit}}", check=False)


def _ci_conclusions(webops: Path, commit: str) -> list[str]:
    """The conclusions of the tooling workflow's runs on a commit, from GitHub."""
    proc = subprocess.run(["gh", "run", "list", "--commit", commit, "--workflow", "tooling.yml",
                           "--json", "conclusion", "--limit", "20"], cwd=webops,
                          capture_output=True, text=True)
    if proc.returncode:
        return []
    return [run["conclusion"] for run in json.loads(proc.stdout)]


def check(ctx: Context, ci_conclusions=_ci_conclusions) -> list[str]:
    """Everything that must hold before a release is built, all reported at once."""
    try:
        ctx.version
    except (release_version.ReleaseVersionError, repo.RepoError) as e:
        return [str(e)]
    commit = tag_commit(ctx)
    if not commit:
        return [f"there is no tag {ctx.tag} in {ctx.webops}"]
    problems = []
    ref = f"refs/tags/{ctx.tag}"
    listed = _git(ctx.webops, "ls-remote", "origin", ref, f"{ref}^{{}}", check=False)
    remote = dict(reversed(line.split("\t", 1)) for line in listed.splitlines())
    if remote.get(f"{ref}^{{}}", remote.get(ref)) != commit:
        problems.append(f"the tag {ctx.tag} is not on origin as {commit[:12]}: push it")
    _git(ctx.webops, "fetch", "-q", "origin", "main", check=False)
    on_main = subprocess.run(["git", "-C", str(ctx.webops), "merge-base", "--is-ancestor",
                              commit, "refs/remotes/origin/main"], capture_output=True)
    if on_main.returncode:
        problems.append(f"the tag's commit {commit[:12]} is not on origin's main")
    if "success" not in ci_conclusions(ctx.webops, commit):
        problems.append(f"the tooling workflow has not passed on {commit[:12]}")
    for name, path in (("this repository", ctx.webops), ("src/ghost", ctx.src / "ghost")):
        head = _git(path, "rev-parse", "HEAD", check=False)
        if head != commit:
            problems.append(f"{name} is at {head[:12] or 'nothing'}, not at the tag's commit "
                            f"{commit[:12]}")
        if _git(path, "status", "--porcelain"):
            problems.append(f"{name} has uncommitted or untracked files")
    if _git(ctx.src, "status", "--porcelain", "--untracked-files=no"):
        problems.append(f"{ctx.src} has local changes, which no release may build: "
                        "commit them to the series or check them out")
    args = ctx.out / "args.gn"
    if args.exists() and args.read_text(encoding="utf-8") != render_args(ctx.identity,
                                                                         ctx.update_url):
        problems.append(f"{args} differs from this release's configuration: find out what "
                        "changed it")
    return problems
