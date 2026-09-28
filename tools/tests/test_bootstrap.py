# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import contextlib
import io
import subprocess
import unittest
from pathlib import Path

import bootstrap
import repo
from check_env import VolumeFacts
from tests.gitutil import GitTestCase

REQ = repo.load_requirements()
ROOT = Path(r"D:\ghost") if bootstrap.sys.platform == "win32" else Path("/work/ghost")


class RenderGclientTest(unittest.TestCase):
    def evaluate(self, text: str) -> dict:
        scope: dict = {}
        exec(compile(text, ".gclient", "exec"), {}, scope)  # gclient evaluates it as Python.
        return scope

    def test_two_unmanaged_solutions(self):
        scope = self.evaluate(bootstrap.render_gclient("https://c.example/src.git",
                                                       "file:///C:/ghost", pgo=False))
        self.assertEqual([s["name"] for s in scope["solutions"]], ["src", "src/ghost"])
        self.assertEqual([s["managed"] for s in scope["solutions"]], [False, False])
        self.assertEqual(scope["solutions"][1]["url"], "file:///C:/ghost")
        self.assertEqual(scope["target_os"], ["win"])

    def test_pgo_flag_controls_profile_download(self):
        for pgo in (True, False):
            scope = self.evaluate(bootstrap.render_gclient("u", "g", pgo=pgo))
            self.assertIs(scope["solutions"][0]["custom_vars"]["checkout_pgo_profiles"], pgo)


class PlanTest(unittest.TestCase):
    def make(self, have_depot_tools):
        return bootstrap.plan(ROOT, "152.0.7977.140", "file:///g", ROOT / "depot_tools",
                              have_depot_tools=have_depot_tools, jobs=12, pgo=False)

    def test_clones_depot_tools_only_when_missing(self):
        self.assertEqual(self.make(False)[0].argv[:2], ("git", "clone"))
        self.assertNotIn("git", [s.argv[0] for s in self.make(True) if s.argv])

    def test_sync_pins_the_tag(self):
        sync = next(s for s in self.make(True) if "sync" in s.argv)
        self.assertEqual(Path(sync.argv[0]), bootstrap.gclient_path(ROOT / "depot_tools"))
        self.assertIn("src@refs/tags/152.0.7977.140", sync.argv)
        self.assertIn("--no-history", sync.argv)
        self.assertEqual(sync.cwd, ROOT)

    def test_hooks_run_after_sync(self):
        commands = [s.argv[1] for s in self.make(True) if s.argv]
        self.assertEqual(commands, ["sync", "runhooks"])


class ValidateRootTest(unittest.TestCase):
    GOOD = VolumeFacts(str(ROOT), 400.0, "ReFS")

    def problems(self, root=ROOT, volume=GOOD, env=None):
        return bootstrap.validate_root(root, volume, REQ, env or {})

    def test_good_root(self):
        self.assertEqual(self.problems(), [])

    def test_rejects_relative_and_spaced_paths(self):
        self.assertTrue(self.problems(root=Path("ghost")))
        self.assertTrue(self.problems(root=ROOT.parent / "my ghost"))

    def test_rejects_onedrive(self):
        self.assertTrue(self.problems(root=ROOT / "inner", env={"OneDrive": str(ROOT)}))

    def test_rejects_small_or_wrong_volume(self):
        self.assertTrue(self.problems(volume=VolumeFacts(str(ROOT), 53.0, "NTFS")))
        self.assertTrue(self.problems(volume=VolumeFacts(str(ROOT), 900.0, "FAT32")))


class ExecuteTest(GitTestCase):
    def test_append_line_is_idempotent(self):
        target = self.tmp / "info" / "exclude"
        target.parent.mkdir()
        target.write_text("# existing\n")
        step = bootstrap.Step("x", append_line=(target, "/ghost/"))
        bootstrap.execute(step, {})
        bootstrap.execute(step, {})
        self.assertEqual(target.read_text(), "# existing\n/ghost/\n")

    def test_write_creates_parents(self):
        target = self.tmp / "new" / ".gclient"
        bootstrap.execute(bootstrap.Step("x", write=(target, "solutions = []\n")), {})
        self.assertEqual(target.read_text(), "solutions = []\n")


class DefaultGhostUrlTest(GitTestCase):
    def test_uses_origin_when_configured(self):
        r = self.init_repo("withorigin")
        self.git(r, "remote", "add", "origin", "https://example.invalid/ghost.git")
        self.assertEqual(bootstrap.default_ghost_url(r), "https://example.invalid/ghost.git")

    def test_falls_back_to_file_url(self):
        r = self.init_repo("local")
        self.assertEqual(bootstrap.default_ghost_url(r), r.resolve().as_uri())


class DryRunTest(GitTestCase):
    def test_dry_run_prints_plan_and_writes_nothing(self):
        root = self.tmp / "checkout"
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            code = bootstrap.main(["--root", str(root), "--dry-run", "--ghost-url", "file:///g"])
        self.assertEqual(code, 0)
        self.assertIn(f"src@refs/tags/{repo.read_chromium_version()}", out.getvalue())
        self.assertIn(subprocess.list2cmdline(["git", "clone", bootstrap.DEPOT_TOOLS_URL]),
                      out.getvalue())
        self.assertFalse(root.exists())


if __name__ == "__main__":
    unittest.main()
