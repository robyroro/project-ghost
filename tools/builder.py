#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Builds and tests one commit on the self-hosted Windows builder.

  pr       what every change needs: apply the series, build chrome and the
           Ghost test targets, run ghost_unittests and ghost_browsertests
  nightly  pr, plus the installer, its smoke test in Windows Sandbox, and the
           egress audit

The builder keeps a persistent checkout at --root, in the layout bootstrap.py
creates, and builds into out/ci there, so builds are incremental. The commit
under test comes from --source, the CI workspace, and is checked out into
src/ghost. Chromium is re-synced only when the commit pins another Chromium
revision than the last sync, and the patch series is re-applied only when it
or the pin changed: re-applying rewrites every patched file, and the build
tool rebuilds by modification time. docs/build/build-host.md is the runbook.

Usage: python tools/builder.py pr --root D:\\ghost --commit <sha> --source <dir>
"""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path

import bootstrap
import repo

TOOLS = repo.REPO_ROOT / "tools"
# <root>/.ghost-synced holds the CHROMIUM_COMMIT the checkout was last synced to.
SYNC_STAMP = ".ghost-synced"
# <root>/.ghost-applied holds the series_digest() of the series last applied.
APPLY_STAMP = ".ghost-applied"
TEST_TARGETS = ("ghost_unittests", "ghost_browsertests")
Step = bootstrap.Step


def is_synced(root: Path, pin: str) -> bool:
    stamp = root / SYNC_STAMP
    return stamp.exists() and stamp.read_text(encoding="utf-8").strip() == pin


def series_digest(patches_dir: Path, pin: str) -> str:
    """Identifies the series and the Chromium revision it applies to."""
    digest = hashlib.sha256(pin.encode("ascii"))
    for patch in sorted(patches_dir.glob("*.patch")):
        digest.update(patch.name.encode("utf-8") + b"\0" + patch.read_bytes() + b"\0")
    return digest.hexdigest()


def is_applied(root: Path, digest: str) -> bool:
    stamp = root / APPLY_STAMP
    return stamp.exists() and stamp.read_text(encoding="utf-8").strip() == digest


def is_clean(src: Path) -> bool:
    """Whether Chromium's tracked files are exactly what the branch committed."""
    proc = subprocess.run(["git", "-C", str(src), "status", "--porcelain", "--untracked-files=no"],
                          capture_output=True, text=True)
    return proc.returncode == 0 and not proc.stdout.strip()


def plan(kind: str, root: Path, commit: str, source: Path, out: str, jobs: int | None,
         synced: bool, pin: str, applied: bool, series: str, results: Path, depot_tools: Path,
         python: str) -> list[Step]:
    src = root / "src"
    ghost = src / "ghost"
    out_rel = Path("out") / out
    out_dir = src / out_rel
    steps = [
        Step("Fetch the commit into src/ghost",
             ("git", "-C", str(ghost), "fetch", "--no-tags", str(source), commit)),
        Step("Check out the commit", ("git", "-C", str(ghost), "checkout", "--detach", "--force",
                                      commit)),
        Step("Remove untracked files from src/ghost", ("git", "-C", str(ghost), "clean", "-ffdx")),
    ]
    if not synced:
        steps += [
            Step("Sync Chromium to the pinned revision (hours when the milestone changes)",
                 (python, str(TOOLS / "bootstrap.py"), "--root", str(root), "--depot-tools",
                  str(depot_tools))),
            Step("Record the synced revision", write=(root / SYNC_STAMP, pin + "\n")),
        ]
    if not synced or not applied:
        steps += [
            Step("Apply the patch series",
                 (python, str(TOOLS / "patches.py"), "apply", "--src", str(src), "--force")),
            Step("Record the applied series", write=(root / APPLY_STAMP, series + "\n")),
        ]
    targets = ("chrome",) + TEST_TARGETS + (("mini_installer",) if kind == "nightly" else ())
    steps += [
        Step("Write GN args", write=(out_dir / "args.gn", 'import("//ghost/build/args/dev.gn")\n')),
        Step("Generate build files", (str(depot_tools / "gn.bat"), "gen", str(out_rel)), cwd=src),
        Step("Build " + ", ".join(targets),
             (str(depot_tools / "autoninja.bat"), "-C", str(out_rel))
             + (("-j", str(jobs)) if jobs else ()) + targets, cwd=src),
    ]
    steps += [Step(f"Run {t}", (str(out_dir / f"{t}.exe"),
                                f"--test-launcher-summary-output={results / (t + '.json')}"),
                   cwd=src)
              for t in TEST_TARGETS]
    if kind == "nightly":
        steps += [
            Step("Installer smoke test in Windows Sandbox",
                 (python, str(TOOLS / "installer_smoke.py"), "sandbox", "--installer",
                  str(out_dir / "mini_installer.exe"))),
            # The NetLog holds the builder's addresses: it stays in results/,
            # which is never uploaded.
            Step("Egress audit", (python, str(TOOLS / "egress_audit.py"), "run", "--chrome",
                                  str(out_dir / "chrome.exe"), "--netlog",
                                  str(results / "netlog.json"))),
        ]
    return steps


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("kind", choices=("pr", "nightly"))
    parser.add_argument("--root", type=Path, required=True,
                        help="the builder's checkout root (bootstrap.py --root)")
    parser.add_argument("--commit", required=True, help="the commit to build")
    parser.add_argument("--source", type=Path, required=True,
                        help="a repository that has the commit, e.g. the CI workspace")
    parser.add_argument("--out", default="ci", help="output directory under src/out")
    parser.add_argument("--jobs", type=int, help="parallel build jobs (default: autoninja's)")
    parser.add_argument("--depot-tools", type=Path, help="default: <root>/depot_tools")
    parser.add_argument("--results", type=Path, help="default: <root>/ci-results")
    parser.add_argument("--skip-sync", action="store_true",
                        help="trust that the checkout is synced to the pin (a hand-made checkout)")
    parser.add_argument("--dry-run", action="store_true", help="print the plan and exit")
    args = parser.parse_args(argv)

    root = args.root.resolve()
    depot_tools = (args.depot_tools or root / "depot_tools").resolve()
    results = (args.results or root / "ci-results").resolve()
    pin = repo.read_chromium_commit()
    series = series_digest(repo.REPO_ROOT / "patches", pin)
    steps = plan(args.kind, root=root, commit=args.commit, source=args.source.resolve(),
                 out=args.out, jobs=args.jobs, synced=args.skip_sync or is_synced(root, pin),
                 pin=pin, applied=is_applied(root, series) and is_clean(root / "src"),
                 series=series, results=results, depot_tools=depot_tools, python=sys.executable)

    for i, step in enumerate(steps, 1):
        print(f"[{i}/{len(steps)}] {step.description}\n        {step.render()}")
    if args.dry_run:
        return 0

    results.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="0")
    env["PATH"] = str(depot_tools) + os.pathsep + env.get("PATH", "")
    for i, step in enumerate(steps, 1):
        print(f"\n==> [{i}/{len(steps)}] {step.description}", flush=True)
        try:
            bootstrap.execute(step, env)
        except subprocess.CalledProcessError as e:
            print(f"error: step {i} ({step.description}) failed with exit code {e.returncode}",
                  file=sys.stderr)
            return e.returncode or 1
        except bootstrap.BootstrapError as e:
            print(f"error: step {i}: {e}", file=sys.stderr)
            return 1
    print(f"\n{args.kind} build of {args.commit} passed; results in {results}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
