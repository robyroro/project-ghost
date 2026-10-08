# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import contextlib
import importlib.util
import io
import tempfile
import unittest
import unittest.mock
from pathlib import Path

import repo

_SPEC = importlib.util.spec_from_file_location(
    "compute_build_timestamp", repo.REPO_ROOT / "build" / "compute_build_timestamp.py")
cbt = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(cbt)

COMMIT_TIME = "1790611429\n"


class BuildTimestampTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)
        self.commit_time = self.dir / "LASTCHANGE.committime"
        self.commit_time.write_text(COMMIT_TIME, encoding="utf-8")
        self.version = self.dir / "CHROMIUM_VERSION"

    def timestamp(self, chromium_version: str) -> int:
        self.version.write_text(chromium_version + "\n", encoding="utf-8")
        return cbt.build_timestamp(self.commit_time, self.version)

    def test_adds_the_chromium_release_patch_like_upstream(self):
        # Upstream: the commit time plus chrome/VERSION's PATCH, which at the
        # pin is CHROMIUM_VERSION's.
        self.assertEqual(self.timestamp("152.0.7977.149"), 1790611429 + 149)

    def test_follows_the_pin(self):
        self.assertEqual(self.timestamp("154.0.8037.93"), 1790611429 + 93)

    def test_does_not_read_chrome_version(self):
        # A respin rewrites chrome/VERSION; the timestamp has no input for it.
        self.assertEqual(cbt.COMMIT_TIME.name, "LASTCHANGE.committime")
        self.assertEqual(cbt.CHROMIUM_VERSION, repo.REPO_ROOT / "CHROMIUM_VERSION")


class MainTest(unittest.TestCase):
    def test_unofficial_toolchains_get_upstreams_answer(self):
        with unittest.mock.patch.object(cbt.subprocess, "run") as run:
            run.return_value.returncode = 0
            self.assertEqual(cbt.main(["default"]), 0)
        run.assert_called_once_with([cbt.sys.executable, str(cbt.UPSTREAM), "default"])
        self.assertEqual(cbt.UPSTREAM, repo.REPO_ROOT.parent / "build" / "compute_build_timestamp.py")

    def test_refuses_other_build_types(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(cbt.main(["nightly"]), 2)
        self.assertIn("official|default", err.getvalue())


if __name__ == "__main__":
    unittest.main()
