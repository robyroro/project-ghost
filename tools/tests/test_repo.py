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

    def test_committed_commit_pin_is_a_full_hash(self):
        self.assertRegex(repo.read_chromium_commit(), r"^[0-9a-f]{40}$")

    def test_abbreviated_commit_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "CHROMIUM_COMMIT").write_text("fb7223c1\n")
            with self.assertRaisesRegex(repo.RepoError, "full 40-character"):
                repo.read_chromium_commit(Path(tmp))

    def test_malformed_pin(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "CHROMIUM_VERSION").write_text("152\n")
            with self.assertRaises(repo.RepoError):
                repo.read_chromium_version(Path(tmp))


class BrandingFilesTest(unittest.TestCase):
    def test_reads_branding_key_value_pairs(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "branding").mkdir()
            (root / "branding" / "BRANDING").write_text(
                "PRODUCT_FULLNAME=Example\nCOPYRIGHT=Copyright 2026 A=B\n\nMAC_TEAM_ID=\n",
                encoding="utf-8")
            self.assertEqual(repo.read_branding(root), {
                "PRODUCT_FULLNAME": "Example", "COPYRIGHT": "Copyright 2026 A=B",
                "MAC_TEAM_ID": ""})

    def test_reads_a_top_level_gni_string(self):
        with tempfile.TemporaryDirectory() as d:
            gni = Path(d) / "x.gni"
            gni.write_text('# name = "comment"\nname = "value"\nif (x) {\n  other = "in"\n}\n',
                           encoding="utf-8")
            self.assertEqual(repo.read_gni_string(gni, "name"), "value")
            with self.assertRaises(repo.RepoError):
                repo.read_gni_string(gni, "other")
            with self.assertRaises(repo.RepoError):
                repo.read_gni_string(gni, "missing")


if __name__ == "__main__":
    unittest.main()
