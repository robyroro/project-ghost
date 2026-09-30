# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import tempfile
import unittest
from pathlib import Path

import builder
import repo

ROOT = Path(r"D:\ghost")
SRC = ROOT / "src"
GHOST = SRC / "ghost"
SOURCE = Path(r"D:\actions-runner\_work\project-ghost\project-ghost")
COMMIT = "0123456789abcdef0123456789abcdef01234567"
DEPOT = ROOT / "depot_tools"
RESULTS = ROOT / "ci-results"


def plan(kind="pr", synced=True, applied=False, out="ci", jobs=None):
    return builder.plan(kind, root=ROOT, commit=COMMIT, source=SOURCE, out=out, jobs=jobs,
                        synced=synced, pin="f" * 40, applied=applied, series="d" * 64,
                        results=RESULTS, depot_tools=DEPOT, python="python.exe")


def argvs(steps):
    return [s.argv for s in steps if s.argv]


class PlanTest(unittest.TestCase):
    def test_puts_the_commit_under_test_into_src_ghost(self):
        first = argvs(plan())[:3]
        self.assertEqual(first, [
            ("git", "-C", str(GHOST), "fetch", "--no-tags", str(SOURCE), COMMIT),
            ("git", "-C", str(GHOST), "checkout", "--detach", "--force", COMMIT),
            ("git", "-C", str(GHOST), "clean", "-ffdx"),
        ])

    def test_applies_the_series_over_whatever_the_last_build_left(self):
        # The builder's branch is disposable: --force recreates it.
        self.assertIn(("python.exe", str(builder.TOOLS / "patches.py"), "apply", "--src",
                       str(SRC), "--force"), argvs(plan()))

    def test_reapplies_the_series_only_when_it_changed(self):
        # Re-applying rewrites every patched file, and the build tool decides
        # by modification time: install_modes.h alone reaches over a hundred
        # files. An unchanged series on a clean tree is left as it is.
        apply = ("python.exe", str(builder.TOOLS / "patches.py"), "apply", "--src", str(SRC),
                 "--force")
        self.assertNotIn(apply, argvs(plan(applied=True)))
        steps = plan(applied=False)
        i = next(i for i, s in enumerate(steps) if s.argv == apply)
        self.assertEqual(steps[i + 1].write, (ROOT / builder.APPLY_STAMP, "d" * 64 + "\n"))

    def test_a_sync_always_reapplies(self):
        # Syncing moves src to the pinned revision, whatever was applied before.
        self.assertTrue([a for a in argvs(plan(synced=False, applied=True))
                         if "patches.py" in " ".join(a)])

    def test_syncs_chromium_only_when_the_pin_moved(self):
        self.assertFalse([s for s in plan(synced=True) if "bootstrap.py" in " ".join(s.argv)])
        steps = plan(synced=False)
        sync = next(i for i, s in enumerate(steps) if "bootstrap.py" in " ".join(s.argv))
        self.assertEqual(steps[sync].argv, ("python.exe", str(builder.TOOLS / "bootstrap.py"),
                                            "--root", str(ROOT), "--depot-tools", str(DEPOT)))
        # The stamp is written only after the sync succeeded.
        self.assertEqual(steps[sync + 1].write, (ROOT / builder.SYNC_STAMP, "f" * 40 + "\n"))

    def test_builds_the_dev_configuration_in_its_own_output_directory(self):
        steps = plan()
        self.assertIn((SRC / "out" / "ci" / "args.gn", 'import("//ghost/build/args/dev.gn")\n'),
                      [s.write for s in steps])
        self.assertIn((str(DEPOT / "gn.bat"), "gen", str(Path("out") / "ci")), argvs(steps))
        build = next(a for a in argvs(steps) if a[0] == str(DEPOT / "autoninja.bat"))
        self.assertEqual(build, (str(DEPOT / "autoninja.bat"), "-C", str(Path("out") / "ci"),
                                 "chrome", "ghost_unittests", "ghost_browsertests"))

    def test_jobs_limit_is_passed_to_the_build(self):
        build = next(a for a in argvs(plan(jobs=10)) if a[0] == str(DEPOT / "autoninja.bat"))
        self.assertEqual(build[3:5], ("-j", "10"))

    def test_runs_the_ghost_suites_with_summaries(self):
        runs = [a for a in argvs(plan()) if a[0].endswith("tests.exe")]
        self.assertEqual(runs, [
            (str(SRC / "out" / "ci" / "ghost_unittests.exe"),
             f"--test-launcher-summary-output={RESULTS / 'ghost_unittests.json'}"),
            (str(SRC / "out" / "ci" / "ghost_browsertests.exe"),
             f"--test-launcher-summary-output={RESULTS / 'ghost_browsertests.json'}"),
        ])

    def test_steps_run_in_order(self):
        names = [s.description for s in plan("nightly", synced=False)]
        order = ["Fetch", "Sync", "Apply", "Write GN args", "Generate", "Build", "Run ghost_unit",
                 "Run ghost_browser", "Installer smoke", "Egress audit"]
        positions = [next(i for i, n in enumerate(names) if n.startswith(o)) for o in order]
        self.assertEqual(positions, sorted(positions))

    def test_nightly_adds_the_installer_and_the_audits(self):
        self.assertFalse([a for a in argvs(plan("pr")) if "installer_smoke.py" in " ".join(a)])
        steps = argvs(plan("nightly"))
        build = next(a for a in steps if a[0] == str(DEPOT / "autoninja.bat"))
        self.assertEqual(build[-1], "mini_installer")
        self.assertIn(("python.exe", str(builder.TOOLS / "installer_smoke.py"), "sandbox",
                       "--installer", str(SRC / "out" / "ci" / "mini_installer.exe")), steps)
        self.assertIn(("python.exe", str(builder.TOOLS / "egress_audit.py"), "run", "--chrome",
                       str(SRC / "out" / "ci" / "chrome.exe"), "--netlog",
                       str(RESULTS / "netlog.json")), steps)

    def test_other_output_directories_can_be_reused(self):
        # A developer machine keeps its first output directory: renaming one
        # discards its build state (docs/build/windows.md).
        self.assertIn((str(DEPOT / "gn.bat"), "gen", str(Path("out") / "vanilla")),
                      argvs(plan(out="vanilla")))


class SeriesTest(unittest.TestCase):
    def write(self, tmp, files):
        for name, text in files.items():
            (Path(tmp) / name).write_text(text, encoding="utf-8")

    def test_digest_covers_every_patch_and_the_pin(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.write(tmp, {"0001-a.patch": "one", "0002-b.patch": "two", "README.md": "x"})
            base = builder.series_digest(Path(tmp), "a" * 40)
            self.assertEqual(builder.series_digest(Path(tmp), "a" * 40), base)
            self.assertNotEqual(builder.series_digest(Path(tmp), "b" * 40), base)
            self.write(tmp, {"README.md": "changed"})
            self.assertEqual(builder.series_digest(Path(tmp), "a" * 40), base)
            self.write(tmp, {"0002-b.patch": "two, edited"})
            self.assertNotEqual(builder.series_digest(Path(tmp), "a" * 40), base)
            self.write(tmp, {"0002-b.patch": "two", "0003-c.patch": "three"})
            self.assertNotEqual(builder.series_digest(Path(tmp), "a" * 40), base)

    def test_applied_only_when_the_stamp_matches(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.assertFalse(builder.is_applied(root, "d" * 64))
            (root / builder.APPLY_STAMP).write_text("d" * 64 + "\n")
            self.assertTrue(builder.is_applied(root, "d" * 64))
            self.assertFalse(builder.is_applied(root, "e" * 64))


class SyncStampTest(unittest.TestCase):
    def test_synced_only_when_the_stamp_names_the_pin(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.assertFalse(builder.is_synced(root, "a" * 40))
            (root / builder.SYNC_STAMP).write_text("a" * 40 + "\n")
            self.assertTrue(builder.is_synced(root, "a" * 40))
            self.assertFalse(builder.is_synced(root, "b" * 40))

    def test_pin_comes_from_this_checkout(self):
        self.assertEqual(builder.TOOLS, repo.REPO_ROOT / "tools")


if __name__ == "__main__":
    unittest.main()
