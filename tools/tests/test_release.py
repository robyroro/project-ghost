# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import datetime
import subprocess
import tempfile
import unittest
from pathlib import Path

import crx3
import provenance
import release
import release_state
import signing
import update_server
from tests.gitutil import GitTestCase

TAG = "152.0.7977.149-1"
VERSION_FILE = "MAJOR=152\nMINOR=0\nBUILD=7977\nPATCH=149\n"


class ArgsTest(unittest.TestCase):
    def test_the_release_configuration_and_its_identity(self):
        self.assertEqual(release.render_args("test", None),
                         "# Written by tools/release.py; edit build/args/release.gn instead.\n"
                         'import("//ghost/build/args/release.gn")\n'
                         'ghost_signing_identity = "test"\n')

    def test_an_update_url(self):
        self.assertTrue(release.render_args("test", "https://203.0.113.5/update").endswith(
            'ghost_update_url = "https://203.0.113.5/update"\n'))

    def test_the_browser_app_id(self):
        self.assertEqual(release.browser_appid(), "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}")


class ReleaseRepos(GitTestCase):
    """A pushed, tagged repository; a Chromium checkout with it at src/ghost."""

    def setUp(self):
        super().setUp()
        self.origin = self.tmp / "origin.git"
        subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(self.origin)],
                       check=True)
        self.webops = self.init_repo("webops")
        self.commit(self.webops, {"CHROMIUM_VERSION": "152.0.7977.149\n"}, "Initial")
        self.git(self.webops, "remote", "add", "origin", str(self.origin))
        self.git(self.webops, "tag", "-a", "-m", "Release", TAG)
        self.git(self.webops, "push", "-q", "origin", "main", TAG)
        self.git(self.webops, "fetch", "-q", "origin")
        self.src = self.init_repo("src")
        self.commit(self.src, {"chrome/VERSION": VERSION_FILE, "README.md": "chromium\n"},
                    "Upstream")
        self.git(self.tmp, "clone", "-q", str(self.webops), str(self.src / "ghost"))
        with open(self.src / ".git" / "info" / "exclude", "a", encoding="utf-8") as f:
            f.write("/ghost/\n")
        self.ctx = release.Context(tag=TAG, src=self.src, webops=self.webops,
                                   releases=self.tmp / "releases")

    def check(self, ctx=None, conclusions=("success",)):
        return release.check(ctx or self.ctx,
                             ci_conclusions=lambda webops, commit: list(conclusions))


class CheckTest(ReleaseRepos):
    def test_a_pushed_tag_on_clean_checkouts_passes(self):
        self.assertEqual(self.check(), [])

    def test_a_missing_tag(self):
        ctx = release.Context(tag="152.0.7977.149-3", src=self.src, webops=self.webops)
        self.assertEqual(self.check(ctx), [f"there is no tag 152.0.7977.149-3 in {self.webops}"])

    def test_a_tag_for_another_chromium(self):
        ctx = release.Context(tag="153.0.7000.1-1", src=self.src, webops=self.webops)
        [problem] = self.check(ctx)
        self.assertIn("is for Chromium 153.0.7000.1", problem)

    def test_the_tag_must_be_pushed(self):
        self.git(self.webops, "tag", "-a", "-m", "Release", "152.0.7977.149-2")
        ctx = release.Context(tag="152.0.7977.149-2", src=self.src, webops=self.webops)
        self.assertIn("the tag 152.0.7977.149-2 is not on origin", "\n".join(self.check(ctx)))

    def test_the_tags_commit_must_be_on_origins_main(self):
        self.git(self.webops, "checkout", "-q", "-b", "side")
        self.commit(self.webops, {"side.txt": "x\n"}, "Side")
        self.git(self.webops, "tag", "-a", "-m", "Release", "152.0.7977.149-2")
        self.git(self.webops, "push", "-q", "origin", "side", "152.0.7977.149-2")
        ctx = release.Context(tag="152.0.7977.149-2", src=self.src, webops=self.webops)
        self.assertIn("is not on origin's main", "\n".join(self.check(ctx)))

    def test_the_tooling_workflow_must_have_passed(self):
        self.assertIn("the tooling workflow has not passed",
                      "\n".join(self.check(conclusions=("failure",))))

    def test_this_repository_must_be_at_the_tag(self):
        self.commit(self.webops, {"later.txt": "x\n"}, "Later")
        self.assertIn("this repository is at", "\n".join(self.check()))

    def test_src_ghost_must_be_at_the_tag(self):
        self.commit(self.src / "ghost", {"later.txt": "x\n"}, "Later")
        self.assertIn("src/ghost is at", "\n".join(self.check()))

    def test_untracked_files_fail(self):
        self.write(self.webops, "stray.txt", "x\n")
        self.assertIn("this repository has uncommitted or untracked files",
                      "\n".join(self.check()))

    def test_local_changes_in_chromium_fail(self):
        self.write(self.src, "README.md", "changed\n")
        self.assertIn("has local changes", "\n".join(self.check()))

    def test_another_configuration_in_out_release_fails(self):
        self.write(self.src, "out/release/args.gn", "is_official_build = false\n")
        self.assertIn("args.gn differs", "\n".join(self.check()))

    def test_the_same_configuration_passes(self):
        self.write(self.src, "out/release/args.gn", release.render_args("test", None))
        self.assertEqual(self.check(), [])


SRC = Path(r"C:\w\chromium\src")
DEPOT = Path(r"C:\src\depot_tools")
WEBOPS = release.repo.REPO_ROOT  # its CHROMIUM_VERSION, 152.0.7977.149, gives the versions
CTX = release.Context(tag=TAG, src=SRC, webops=WEBOPS, depot_tools=DEPOT,
                      releases=Path(r"C:\r"), python="python.exe")


def tool(name):
    return str(DEPOT / (name + (".bat" if release.os.name == "nt" else "")))


class CommandTest(unittest.TestCase):
    def test_build(self):
        self.assertEqual(release.build_commands(CTX), [
            [tool("gn"), "gen", str(release.OUT)],
            [tool("autoninja"), "-C", str(release.OUT), "-j", "10", "chrome", "mini_installer",
             "chrome/updater/win/installer:installer", "chrome/updater/win:signing",
             "chrome/updater/win:updater", "ghost_unittests", "ghost_browsertests"]])

    def test_tests_run_on_the_bits_that_ship(self):
        results = Path(r"C:\r\results")
        commands = release.test_commands(CTX, results)
        self.assertEqual(commands[0], [str(SRC / release.OUT / "ghost_unittests.exe"),
                                       f"--test-launcher-summary-output="
                                       f"{results / 'ghost_unittests.json'}"])
        self.assertEqual(commands[1][0], str(SRC / release.OUT / "ghost_browsertests.exe"))
        self.assertEqual(commands[2], ["python.exe", str(WEBOPS / "tools" / "installer_smoke.py"),
                                       "sandbox", "--installer",
                                       str(SRC / release.OUT / "mini_installer.exe")])
        self.assertEqual(commands[3], ["python.exe", str(WEBOPS / "tools" / "egress_audit.py"),
                                       "run", "--chrome", str(SRC / release.OUT / "chrome.exe"),
                                       "--netlog", str(results / "netlog.json")])

    def test_sign_once_for_both_products(self):
        self.assertEqual(release.sign_command(CTX, "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}"), [
            "python.exe", str(WEBOPS / "tools" / "sign_release.py"), "--src", str(SRC),
            "--browser-out", str(release.OUT), "--updater-out", str(release.OUT),
            "--identity", "test", "--output", str(Path(r"C:\r") / TAG / "signed"),
            "--crx", "--offline-installer", "--version", "152.0.7977.14901",
            "--appid", "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}"])

    def test_a_draft_unless_public(self):
        publish, notes = Path(r"C:\r\p"), Path(r"C:\r\notes.md")
        draft = release.draft_command(CTX, publish, notes)
        self.assertEqual(draft[:10], ["gh", "release", "create", TAG, "--verify-tag", "--title",
                                      "Project Ghost 152.0.7977.14901 (test identity)",
                                      "--notes-file", str(notes), "--prerelease"])
        self.assertIn("--draft", draft)
        self.assertEqual(draft[-5:], [str(publish / name) for name in release.PUBLISHED])
        public = release.Context(tag=TAG, src=SRC, webops=WEBOPS, identity="prod", public=True)
        self.assertNotIn("--draft", release.draft_command(public, publish, notes))

    def test_stage_uploads_the_candidate(self):
        ctx = release.Context(tag=TAG, src=SRC, webops=WEBOPS, host="ghost@203.0.113.5",
                              fraction=0.01, python="python.exe")
        self.assertEqual(release.stage_command(ctx, Path(r"C:\r\update.crx3"), "{a}"), [
            "python.exe", "-m", "ghost_update.release", "--crx", str(Path(r"C:\r\update.crx3")),
            "--appid", "{a}", "--version", "152.0.7977.14901", "--identity", "test",
            "--host", "ghost@203.0.113.5", "--fraction", "0.01"])

    def test_admin_commands(self):
        self.assertEqual(release.admin_command("ghost@h", "set-fraction", "{a}", 0.05),
                         ["ssh", "ghost@h", "sudo ghost-update-admin set-fraction --appid '{a}' "
                                            "--fraction 0.05"])
        self.assertEqual(release.admin_command("ghost@h", "halt", "{a}"),
                         ["ssh", "ghost@h", "sudo ghost-update-admin halt --appid '{a}'"])


class NotesTest(unittest.TestCase):
    def test_the_test_identity_is_announced(self):
        notes = release.release_notes(CTX)
        self.assertIn("**Test identity: not for daily use.**", notes)
        self.assertIn("won't migrate", notes)
        self.assertIn("152.0.7977.149", notes)
        for name in release.PUBLISHED:
            self.assertIn(f"`{name}`", notes)


class Recorder:
    """Stands in for the runner: records each command, fails on request."""

    def __init__(self, fail_on: str | None = None):
        self.commands, self.fail_on = [], fail_on

    def __call__(self, argv, cwd=None):
        self.commands.append(list(argv))
        if self.fail_on and any(self.fail_on in str(a) for a in argv):
            raise subprocess.CalledProcessError(1, argv)


def stage(name, log, outputs=None, wanted=lambda ctx: True):
    def perform(ctx, state, run):
        log.append(name)
        return outputs or {name: "done"}
    return release.Stage(name, lambda ctx, state: {"input": name}, perform, wanted)


class RunStagesTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.state = release_state.State.load(Path(tmp.name) / "state.json", TAG)
        self.clock = iter(datetime.datetime(2026, 10, 5, 9, m, tzinfo=datetime.timezone.utc)
                          for m in range(60))

    def run_stages(self, stages, redo=None):
        release.run_stages(CTX, self.state, Recorder(), stages=stages, redo=redo,
                           now=lambda: next(self.clock))

    def test_runs_in_order_and_records(self):
        log = []
        self.run_stages([stage("a", log), stage("b", log)])
        self.assertEqual(log, ["a", "b"])
        self.assertTrue(self.state.is_done("b", {"input": "b"}))

    def test_a_rerun_skips_what_is_done(self):
        log = []
        self.run_stages([stage("a", log), stage("b", log)])
        self.run_stages([stage("a", log), stage("b", log)])
        self.assertEqual(log, ["a", "b"])

    def test_redo_runs_one_stage_again(self):
        log = []
        self.run_stages([stage("a", log), stage("b", log)])
        self.run_stages([stage("a", log), stage("b", log)], redo="a")
        self.assertEqual(log, ["a", "b", "a"])

    def test_a_failed_stage_is_not_recorded(self):
        def fail(ctx, state, run):
            raise release.ReleaseError("no")
        with self.assertRaises(release.ReleaseError):
            self.run_stages([release.Stage("a", lambda c, s: {}, fail, lambda c: True)])
        self.assertNotIn("a", self.state.stages)

    def test_an_unwanted_stage_is_skipped(self):
        log = []
        self.run_stages([stage("a", log, wanted=lambda ctx: False)])
        self.assertEqual(log, [])

    def test_the_stage_stage_runs_only_with_a_fraction(self):
        stage_stage = next(s for s in release.STAGES if s.name == "stage")
        self.assertFalse(stage_stage.wanted(CTX))
        self.assertTrue(stage_stage.wanted(release.Context(tag=TAG, src=SRC, webops=WEBOPS,
                                                           fraction=0.0, host="h")))

    def test_the_stage_names(self):
        self.assertEqual([s.name for s in release.STAGES],
                         ["sync", "apply", "build", "test", "sign", "describe", "draft",
                          "stage"])


class BuildStageTest(ReleaseRepos):
    def test_chrome_version_is_restored_when_the_build_fails(self):
        run = Recorder(fail_on="autoninja")
        state = release_state.State.load(self.tmp / "state.json", TAG)
        state.record("apply", {}, {"series": "d" * 64}, datetime.datetime.now(),
                     datetime.datetime.now())
        ctx = release.Context(tag=TAG, src=self.src, webops=self.webops, python="python")

        def write_version(argv, cwd=None):
            run(argv, cwd)
            if "release_version.py" in " ".join(argv):
                self.write(self.src, "chrome/VERSION", VERSION_FILE.replace("PATCH=149",
                                                                            "PATCH=14901"))

        with self.assertRaises(subprocess.CalledProcessError):
            release.build_perform(ctx, state, write_version)
        self.assertEqual((self.src / "chrome" / "VERSION").read_text(), VERSION_FILE)
        self.assertEqual((ctx.out / "args.gn").read_text(), release.render_args("test", None))


FACTS = provenance.BuildFacts(
    tag=TAG, webops_commit="a" * 40, chromium_commit="b" * 40, series_digest="c" * 64,
    depot_tools_commit="d" * 40, toolchain={"clang": "x"}, args_gn="", identity="dev",
    tests={}, started=datetime.datetime(2026, 10, 5, tzinfo=datetime.timezone.utc),
    finished=datetime.datetime(2026, 10, 6, tzinfo=datetime.timezone.utc))


class VerifyTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)
        developer = signing.file_signer(update_server.CRX_KEY_FILE)
        publisher = signing.file_signer(update_server.CRX_BACKUP_KEY_FILE)  # pinned for dev
        (self.dir / "update.crx3").write_bytes(
            crx3.build({"mini_installer.exe": b"installer"}, developer, [publisher]))
        provenance.write_release_files(self.dir, ["update.crx3"], FACTS)

    def test_a_good_development_release(self):
        self.assertEqual(release.verify(self.dir, "dev"), [])

    def test_a_proof_by_another_key_fails(self):
        # crx_test_key.json is also dev's primary publisher key: a developer key
        # nobody pins, so that no proof in the package is a pinned one.
        developer = signing.scalar_signer("developer", 0x1234567890ABCDEF)
        other = signing.scalar_signer("other", update_server.OTHER_KEY)
        (self.dir / "update.crx3").write_bytes(
            crx3.build({"mini_installer.exe": b"installer"}, developer, [other]))
        provenance.write_release_files(self.dir, ["update.crx3"], FACTS)
        self.assertEqual(release.verify(self.dir, "dev"),
                         ["update.crx3: no valid proof by the dev identity's publisher keys"])

    def test_hash_failures_are_reported(self):
        (self.dir / "update.crx3").write_bytes(b"changed")
        self.assertIn("update.crx3: its SHA-256 differs from SHA256SUMS",
                      release.verify(self.dir, "dev"))
