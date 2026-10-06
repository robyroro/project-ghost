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

import argparse
import datetime
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import builder
import crx3
import offline_installer
import patches
import provenance
import release_state
import release_version
import repo
import sbom
import signing

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


SHIPPED_TARGETS = ("chrome", "mini_installer", "chrome/updater/win/installer:installer",
                   "chrome/updater/win:signing", "chrome/updater/win:updater")
# Where ghost_browsertests is built and run: the development build. In the
# official configuration it is about 14,500 more actions (the browser's test
# support code, then a ThinLTO link): four hours or more on the reference
# machine, against minutes in out/vanilla (progress notes). The provenance
# records where each suite ran.
BROWSERTESTS_OUT = Path("out") / "vanilla"
SIGNED = "signed"
PUBLISH = "publish"
PUBLISHED = (offline_installer.OUTPUT_NAME, "update.crx3", sbom.DOCUMENT,
             provenance.PROVENANCE_FILE, provenance.SUMS_FILE)


def _tool(depot_tools: Path, name: str) -> str:
    return str(depot_tools / (f"{name}.bat" if os.name == "nt" else name))


def build_targets() -> tuple[str, ...]:
    tests = ("ghost_unittests",) + (("ghost_browsertests",) if BROWSERTESTS_OUT == OUT else ())
    return SHIPPED_TARGETS + tests


def build_commands(ctx: Context) -> list[list[str]]:
    return [[_tool(ctx.depot_tools, "gn"), "gen", str(OUT)],
            [_tool(ctx.depot_tools, "autoninja"), "-C", str(OUT), "-j", str(ctx.jobs),
             *build_targets()]]


def test_commands(ctx: Context, results: Path) -> list[list[str]]:
    commands = []
    if BROWSERTESTS_OUT != OUT:
        commands.append([_tool(ctx.depot_tools, "autoninja"), "-C", str(BROWSERTESTS_OUT),
                         "-j", str(ctx.jobs), "ghost_browsertests"])
    for suite, out in (("ghost_unittests", OUT), ("ghost_browsertests", BROWSERTESTS_OUT)):
        commands.append([str(ctx.src / out / f"{suite}.exe"),
                         f"--test-launcher-summary-output={results / (suite + '.json')}"])
    tools = ctx.webops / "tools"
    commands.append([ctx.python, str(tools / "installer_smoke.py"), "sandbox", "--installer",
                     str(ctx.out / "mini_installer.exe")])
    # The NetLog holds this machine's addresses: it stays in the release
    # directory, which is never published.
    commands.append([ctx.python, str(tools / "egress_audit.py"), "run", "--chrome",
                     str(ctx.out / "chrome.exe"), "--netlog", str(results / "netlog.json")])
    return commands


def sign_command(ctx: Context, appid: str) -> list[str]:
    return [ctx.python, str(ctx.webops / "tools" / "sign_release.py"), "--src", str(ctx.src),
            "--browser-out", str(OUT), "--updater-out", str(OUT), "--identity", ctx.identity,
            "--output", str(ctx.dir / SIGNED), "--crx", "--offline-installer",
            "--version", ctx.version, "--appid", appid]


def draft_command(ctx: Context, publish: Path, notes: Path) -> list[str]:
    title = f"Project Ghost {ctx.version}" + ("" if ctx.public else f" ({ctx.identity} identity)")
    command = ["gh", "release", "create", ctx.tag, "--verify-tag", "--title", title,
               "--notes-file", str(notes), "--prerelease"]
    if not ctx.public:
        command.append("--draft")
    return command + [str(publish / name) for name in PUBLISHED]


def stage_command(ctx: Context, crx: Path, appid: str) -> list[str]:
    return [ctx.python, "-m", "ghost_update.release", "--crx", str(crx), "--appid", appid,
            "--version", ctx.version, "--identity", ctx.identity, "--host", ctx.host,
            "--fraction", str(ctx.fraction)]


def admin_command(host: str, action: str, appid: str, fraction: float | None = None) -> list[str]:
    args = ["sudo", "ghost-update-admin", action, "--appid", appid]
    if fraction is not None:
        args += ["--fraction", str(fraction)]
    return ["ssh", host, shlex.join(args)]


def release_notes(ctx: Context) -> str:
    lines = [f"Project Ghost {ctx.version}: Chromium {ctx.chromium_version} with Ghost's patch "
             f"series, from the tag `{ctx.tag}`.", ""]
    if ctx.identity in NOT_PUBLIC:
        lines += ["**Test identity: not for daily use.** This build carries Phase 2's "
                  f"{ctx.identity} identity: test app IDs, a certificate only test machines "
                  "trust, and test update keys. Installs of it won't migrate to the final "
                  "product name.", ""]
    lines += [
        "| File | What it is |", "|---|---|",
        f"| `{offline_installer.OUTPUT_NAME}` | The signed offline installer |",
        "| `update.crx3` | The update package, with Ghost's publisher proof |",
        f"| `{sbom.DOCUMENT}` | The SBOM (SPDX 2.2) |",
        f"| `{provenance.PROVENANCE_FILE}` | How it was built (SLSA Build Level 1, unsigned) |",
        f"| `{provenance.SUMS_FILE}` | The SHA-256 of each file |", "",
        "Check them with `python tools/release.py verify <directory>` "
        "([docs/build/release.md](https://github.com/robyroro/project-ghost/blob/main/docs/"
        "build/release.md)).",
    ]
    return "\n".join(lines) + "\n"


Runner = Callable[..., None]  # run(argv, cwd=None); raises CalledProcessError


def _env(ctx: Context) -> dict[str, str]:
    env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="0")
    env["PATH"] = str(ctx.depot_tools) + os.pathsep + env.get("PATH", "")
    return env


def runner_for(ctx: Context) -> Runner:
    env = _env(ctx)

    def run(argv: list[str], cwd: Path | None = None) -> None:
        print("    " + subprocess.list2cmdline([str(a) for a in argv]), flush=True)
        subprocess.run(argv, cwd=cwd, env=env, check=True)
    return run


@dataclass(frozen=True)
class Stage:
    name: str
    inputs: Callable[[Context, release_state.State], dict]
    perform: Callable[[Context, release_state.State, Runner], dict]
    wanted: Callable[[Context], bool] = lambda ctx: True


def _pin(ctx: Context) -> str:
    return repo.read_chromium_commit(ctx.webops)


def _series(ctx: Context) -> str:
    return builder.series_digest(ctx.webops / "patches", _pin(ctx))


def pgo_profile(ctx: Context) -> str:
    return (ctx.src / "chrome" / "build" / "win64.pgo.txt").read_text(encoding="utf-8").strip()


def sync_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    pin = _pin(ctx)
    if not builder.is_synced(ctx.root, pin):
        run([ctx.python, str(ctx.webops / "tools" / "bootstrap.py"), "--root", str(ctx.root),
             "--depot-tools", str(ctx.depot_tools), "--pgo"])
        (ctx.root / builder.SYNC_STAMP).write_text(pin + "\n", encoding="utf-8")
    # The two DEPS hooks an official x64 build needs, which checkout_pgo_profiles
    # turns on: Chrome's win64 profile and V8's builtins profiles. Both are
    # idempotent, and quick when the profiles are there.
    vpython = _tool(ctx.depot_tools, "vpython3")
    run([vpython, "tools/update_pgo_profiles.py", "--target=win64", "update",
         "--gs-url-base=chromium-optimization-profiles/pgo_profiles"], cwd=ctx.src)
    run([vpython, "v8/tools/builtins-pgo/download_profiles.py", "download", "--depot-tools",
         "third_party/depot_tools", "--check-v8-revision", "--quiet"], cwd=ctx.src)
    return {"pin": pin, "profile": pgo_profile(ctx)}


def apply_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    patches_dir = ctx.webops / "patches"
    if not patches.series_matches(ctx.src, ctx.base, patches_dir):
        run([ctx.python, str(ctx.webops / "tools" / "patches.py"), "apply", "--src",
             str(ctx.src), "--force"])
        if not patches.series_matches(ctx.src, ctx.base, patches_dir):
            raise ReleaseError("the series applied, but the branch still differs from patches/")
    (ctx.root / builder.APPLY_STAMP).write_text(_series(ctx) + "\n", encoding="utf-8")
    return {"series": _series(ctx)}


def build_inputs(ctx: Context, state: release_state.State) -> dict:
    return {"commit": tag_commit(ctx), "args.gn": render_args(ctx.identity, ctx.update_url),
            "series": state.outputs("apply")["series"], "targets": " ".join(build_targets())}


def build_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    args = ctx.out / "args.gn"
    wanted = render_args(ctx.identity, ctx.update_url)
    if not args.exists() or args.read_text(encoding="utf-8") != wanted:
        args.parent.mkdir(parents=True, exist_ok=True)
        args.write_text(wanted, encoding="utf-8", newline="\n")
    try:
        run([ctx.python, str(ctx.webops / "tools" / "release_version.py"), "write", "--src",
             str(ctx.src), "--tag", ctx.tag])
        for argv in build_commands(ctx):
            run(argv, cwd=ctx.src)
    finally:
        _git(ctx.src, "checkout", "--", "chrome/VERSION")
    return {name: provenance.sha256_file(ctx.out / name)
            for name in ("mini_installer.exe", "UpdaterSetup.exe")}


def test_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    results = ctx.dir / "results"
    results.mkdir(parents=True, exist_ok=True)
    for argv in test_commands(ctx, results):
        run(argv, cwd=ctx.src)
    return {"ghost_unittests": OUT.as_posix(), "ghost_browsertests": BROWSERTESTS_OUT.as_posix()}


def sign_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    signed = ctx.dir / SIGNED
    if signed.exists():
        shutil.rmtree(signed)  # this stage's own output, from an earlier run
    run(sign_command(ctx, browser_appid(ctx.webops)), cwd=ctx.src)
    return {name: provenance.sha256_file(signed / name)
            for name in ("mini_installer.exe", "update.crx3", offline_installer.OUTPUT_NAME)}


def toolchain(ctx: Context) -> dict[str, str]:
    import check_env
    versions = {"clang": (ctx.src / "third_party" / "llvm-build" / "Release+Asserts"
                          / "cr_build_revision").read_text(encoding="utf-8").strip()}
    sdks = check_env.probe_sdk_include_versions()
    if sdks:
        versions["windows-sdk"] = max(sdks, key=repo.parse_version)
    _, _, satisfying = check_env.probe_vs(repo.load_requirements(ctx.webops)["windows"]
                                          ["visual_studio"])
    if satisfying:
        versions["visual-studio"] = satisfying[0].version
    return versions


def describe_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    publish = ctx.dir / PUBLISH
    if publish.exists():
        shutil.rmtree(publish)  # this stage's own output, from an earlier run
    publish.mkdir(parents=True)
    for name in (offline_installer.OUTPUT_NAME, "update.crx3"):
        shutil.copy2(ctx.dir / SIGNED / name, publish / name)
    sbom.build(_tool(ctx.depot_tools, "vpython3"), ctx.src, ctx.out, ctx.tag, tag_commit(ctx),
               publish / sbom.DOCUMENT, env=_env(ctx))
    build = state.stages["build"]
    facts = provenance.BuildFacts(
        tag=ctx.tag, webops_commit=tag_commit(ctx), chromium_commit=_pin(ctx),
        series_digest=state.outputs("apply")["series"],
        depot_tools_commit=_git(ctx.depot_tools, "rev-parse", "HEAD"), toolchain=toolchain(ctx),
        args_gn=render_args(ctx.identity, ctx.update_url), identity=ctx.identity,
        tests=state.outputs("test"), started=datetime.datetime.fromisoformat(build["started"]),
        finished=datetime.datetime.fromisoformat(build["finished"]))
    provenance.write_release_files(
        publish, [offline_installer.OUTPUT_NAME, "update.crx3", sbom.DOCUMENT], facts)
    return provenance.parse_sums((publish / provenance.SUMS_FILE).read_text(encoding="utf-8"))


def _gh(ctx: Context, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["gh", *args], cwd=ctx.webops, capture_output=True)


def draft_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    publish = ctx.dir / PUBLISH
    notes = ctx.dir / "notes.md"
    notes.write_text(release_notes(ctx), encoding="utf-8", newline="\n")
    ours = (publish / provenance.SUMS_FILE).read_bytes()
    existing = _gh(ctx, "release", "view", ctx.tag, "--json", "assets")
    if existing.returncode == 0:
        assets = {a["name"]: a for a in json.loads(existing.stdout)["assets"]}
        sums = assets.get(provenance.SUMS_FILE)
        theirs = _gh(ctx, "api", "-H", "Accept: application/octet-stream",
                     sums["apiUrl"]).stdout if sums else b""
        if set(assets) != set(PUBLISHED) or theirs != ours:
            raise ReleaseError(f"GitHub already has a release {ctx.tag} with other files; "
                               "compare it with this one before changing either")
    else:
        run(draft_command(ctx, publish, notes), cwd=ctx.webops)
    view = _gh(ctx, "release", "view", ctx.tag, "--json", "url")
    return {"url": json.loads(view.stdout)["url"] if view.returncode == 0 else ""}


def stage_perform(ctx: Context, state: release_state.State, run: Runner) -> dict:
    run(stage_command(ctx, ctx.dir / PUBLISH / "update.crx3", browser_appid(ctx.webops)),
        cwd=ctx.server_repo)
    return {"fraction": str(ctx.fraction), "host": ctx.host}


STAGES = (
    Stage("sync", lambda ctx, state: {"pin": _pin(ctx)}, sync_perform),
    Stage("apply", lambda ctx, state: {"series": _series(ctx), "pin": _pin(ctx)},
          apply_perform),
    Stage("build", build_inputs, build_perform),
    Stage("test", lambda ctx, state: dict(state.outputs("build")), test_perform),
    Stage("sign", lambda ctx, state: {**state.outputs("build"), "identity": ctx.identity},
          sign_perform),
    Stage("describe", lambda ctx, state: {**state.outputs("sign"),
                                          **{f"test {k}": v for k, v
                                             in state.outputs("test").items()}},
          describe_perform),
    Stage("draft", lambda ctx, state: {**state.outputs("describe"), "public": str(ctx.public)},
          draft_perform),
    Stage("stage", lambda ctx, state: {"update.crx3": state.outputs("sign")["update.crx3"],
                                       "fraction": str(ctx.fraction), "host": str(ctx.host)},
          stage_perform, wanted=lambda ctx: ctx.fraction is not None),
)


def _now() -> datetime.datetime:
    return datetime.datetime.now(datetime.timezone.utc)


def run_stages(ctx: Context, state: release_state.State, run: Runner,
               stages=STAGES, redo: str | None = None, now=_now) -> None:
    for stage in stages:
        if not stage.wanted(ctx):
            continue
        if stage.name == redo:
            state.forget(stage.name)
        inputs = stage.inputs(ctx, state)
        if state.is_done(stage.name, inputs):
            print(f"[{stage.name}] done before; skipped", flush=True)
            continue
        print(f"[{stage.name}]", flush=True)
        started = now()
        outputs = stage.perform(ctx, state, run)
        state.record(stage.name, inputs, outputs, started, now())


def verify(directory: Path, identity: str) -> list[str]:
    """What anyone with a release's files can check: hashes, provenance, signatures."""
    failures = provenance.check_files(directory)
    crx = directory / "update.crx3"
    if crx.is_file():
        pinned = signing.pinned_keys(identity).publisher_hashes
        try:
            keys = crx3.verified_keys(crx.read_bytes())
        except (ValueError, IndexError):
            keys = []
        if not any(hashlib.sha256(key).digest() in pinned for key in keys):
            failures.append(f"update.crx3: no valid proof by the {identity} identity's "
                            "publisher keys")
    setup = directory / offline_installer.OUTPUT_NAME
    if setup.is_file() and identity != "dev":
        if os.name != "nt":
            failures.append(f"{setup.name}: Authenticode can only be checked on Windows")
        else:
            import authenticode
            certificate = (repo.REPO_ROOT / "branding" / "signing"
                           / f"{identity}_codesign.cer").read_bytes()
            failures += authenticode.verify([setup], authenticode.thumbprint(certificate),
                                            require_trusted=False)
    return failures


def _fraction(text: str) -> float:
    value = float(text)
    if not 0 <= value <= 1:
        raise argparse.ArgumentTypeError("a fraction is between 0 and 1")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run", help="make a release from a tag")
    r.add_argument("--tag", required=True)
    r.add_argument("--src", type=Path, required=True, help="the Chromium checkout's src")
    r.add_argument("--identity", choices=("dev", "test"), default="test")
    r.add_argument("--update-url", help="the update server's URL the build uses")
    r.add_argument("--redo", choices=[s.name for s in STAGES])
    r.add_argument("--stage", type=_fraction, metavar="FRACTION",
                   help="offer the release to this fraction of update checks")
    r.add_argument("--host", help="USER@HOST of the update server")
    r.add_argument("--public", action="store_true", help="a public release, not a draft")
    r.add_argument("--releases", type=Path, default=DEFAULT_RELEASES)
    r.add_argument("--depot-tools", type=Path, default=DEFAULT_DEPOT_TOOLS)
    r.add_argument("--server-repo", type=Path, default=DEFAULT_SERVER_REPO)
    r.add_argument("--jobs", type=int, default=10)
    v = sub.add_parser("verify", help="check a release's files")
    v.add_argument("directory", type=Path)
    v.add_argument("--identity", choices=("dev", "test"), default="test")
    for action in ("rollout", "halt", "promote", "drop"):
        a = sub.add_parser(action, help=f"{action} the update server's candidate")
        a.add_argument("--host", required=True, help="USER@HOST of the update server")
        a.add_argument("--appid", default=None, help="default: branding/updater.gni")
        if action == "rollout":
            a.add_argument("--fraction", type=_fraction, required=True)
    try:
        args = parser.parse_args(argv)
    except SystemExit as e:
        return int(e.code or 0)

    if args.command == "verify":
        failures = verify(args.directory, args.identity)
        for failure in failures:
            print(f"FAILED  {failure}")
        print("verified" if not failures else f"{len(failures)} failure(s)")
        return 1 if failures else 0
    if args.command != "run":
        appid = args.appid or browser_appid()
        action = "set-fraction" if args.command == "rollout" else args.command
        command = admin_command(args.host, action, appid, getattr(args, "fraction", None))
        return 0 if subprocess.run(command).returncode == 0 else 1

    if args.public and args.identity in NOT_PUBLIC:
        print(f"release: the {args.identity} identity never makes a public release "
              "(docs/licensing.md#release-gates)", file=sys.stderr)
        return 2
    if (args.stage is None) != (args.host is None):
        print("release: --stage and --host go together", file=sys.stderr)
        return 2
    ctx = Context(tag=args.tag, src=args.src.resolve(), identity=args.identity,
                  update_url=args.update_url, depot_tools=args.depot_tools,
                  releases=args.releases, server_repo=args.server_repo, host=args.host,
                  fraction=args.stage, public=args.public, jobs=args.jobs)
    problems = check(ctx)
    for problem in problems:
        print(f"release: {problem}", file=sys.stderr)
    if problems:
        return 1
    try:
        state = release_state.State.load(ctx.dir / "state.json", ctx.tag)
        run_stages(ctx, state, runner_for(ctx), redo=args.redo)
    except (subprocess.CalledProcessError, ReleaseError, release_state.StateError,
            OSError) as e:
        print(f"release: {e}", file=sys.stderr)
        return 1
    print(f"release {ctx.tag} ({ctx.version}) done; its files are in {ctx.dir / PUBLISH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
