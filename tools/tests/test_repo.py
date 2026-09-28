# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import tempfile
import unittest
from pathlib import Path

import repo


class VersionTest(unittest.TestCase):
    def test_parse_and_compare(self):
        self.assertEqual(repo.parse_version("10.0.26100.7705"), (10, 0, 26100, 7705))
        self.assertTrue(repo.version_at_least("10.0.26100.7705", "10.0.26100.3323"))
        self.assertFalse(repo.version_at_least("10.0.22621.1", "10.0.26100.3323"))
        self.assertTrue(repo.version_at_least("18.0", "17.14.1"))
        self.assertTrue(repo.version_at_least("17.14", "17.14.0.0"))

    def test_rejects_non_numeric(self):
        for bad in ("", "10.x", "v1.2", "1..2"):
            with self.assertRaises(ValueError, msg=bad):
                repo.parse_version(bad)


class PinnedFilesTest(unittest.TestCase):
    def test_committed_pin_and_requirements_agree(self):
        version = repo.read_chromium_version()
        self.assertEqual(repo.load_requirements()["chromium_version"], version)

    def test_mismatch_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            (root / "CHROMIUM_VERSION").write_text("153.0.1.1\n")
            (root / "build" / "requirements.json").write_text(
                json.dumps({"chromium_version": "152.0.7977.140"}))
            with self.assertRaisesRegex(repo.RepoError, "re-derive the requirements"):
                repo.load_requirements(root)

    def test_malformed_pin(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "CHROMIUM_VERSION").write_text("152\n")
            with self.assertRaises(repo.RepoError):
                repo.read_chromium_version(Path(tmp))


if __name__ == "__main__":
    unittest.main()
