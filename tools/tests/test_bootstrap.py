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


COMMIT = "fb7223c1c4b6365c1308b1796007d10b744ac216"


class PlanTest(unittest.TestCase):
    def make(self, have_depot_tools=True, have_src=False):
        return bootstrap.plan(ROOT, "152.0.7977.140", COMMIT, "file:///g", ROOT / "depot_tools",
                              have_depot_tools=have_depot_tools, have_src=have_src, jobs=12,
                              pgo=False)

    def kinds(self, steps):
        return [s.argv[1] if s.argv and s.argv[0] == "git" and s.argv[1] == "clone"
                else s.argv[3] if s.argv and s.argv[0] == "git"
                else s.argv[1] if s.argv
                else "write" if s.write else "verify" if s.verify_ref else "append"
                for s in steps]

    def test_clones_depot_tools_only_when_missing(self):
        self.assertEqual(self.make(have_depot_tools=False)[0].argv[:2], ("git", "clone"))
        self.assertNotIn("clone", self.kinds(self.make()))

    def test_sync_pins_the_commit_not_the_tag(self):
        # A tag revision makes gclient fetch every upstream branch first.
        sync = next(s for s in self.make() if "sync" in s.argv)
        self.assertEqual(Path(sync.argv[0]), bootstrap.gclient_path(ROOT / "depot_tools"))
        self.assertIn(f"src@{COMMIT}", sync.argv)
        self.assertIn("--no-history", sync.argv)
        self.assertEqual(sync.cwd, ROOT)

    def test_existing_checkout_fetches_and_verifies_tag_before_sync(self):
        self.assertEqual(self.kinds(self.make(have_src=True)),
                         ["write", "fetch", "verify", "sync", "runhooks", "append"])

    def test_fresh_checkout_verifies_tag_after_clone(self):
        self.assertEqual(self.kinds(self.make(have_src=False)),
                         ["write", "sync", "fetch", "verify", "runhooks", "append"])

    def test_tag_fetch_is_shallow_and_targeted(self):
        fetch = next(s for s in self.make(have_src=True) if "fetch" in s.argv)
        self.assertIn("--depth=1", fetch.argv)
        self.assertEqual(fetch.argv[-1],
                         "+refs/tags/152.0.7977.140:refs/tags/152.0.7977.140")


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

    def test_verify_ref_accepts_the_pinned_commit(self):
        r = self.init_repo("pinned")
        sha = self.commit(r, {"a.txt": "a\n"}, "a")
        self.git(r, "tag", "1.0.0.0")
        bootstrap.execute(bootstrap.Step("x", verify_ref=(r, "refs/tags/1.0.0.0", sha)), {})

    def test_verify_ref_rejects_a_moved_tag(self):
        r = self.init_repo("moved")
        pinned = self.commit(r, {"a.txt": "a\n"}, "a")
        self.commit(r, {"a.txt": "b\n"}, "b")
        self.git(r, "tag", "1.0.0.0")
        with self.assertRaisesRegex(bootstrap.BootstrapError, "upstream tag moved"):
            bootstrap.execute(bootstrap.Step("x", verify_ref=(r, "refs/tags/1.0.0.0", pinned)),
                              {})

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
        self.assertIn(f"src@{repo.read_chromium_commit()}", out.getvalue())
        self.assertIn(subprocess.list2cmdline(["git", "clone", bootstrap.DEPOT_TOOLS_URL]),
                      out.getvalue())
        self.assertFalse(root.exists())


if __name__ == "__main__":
    unittest.main()
