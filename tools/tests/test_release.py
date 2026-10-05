# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import subprocess
import unittest
from pathlib import Path

import release
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
