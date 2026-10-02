# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import contextlib
import io
import tempfile
import unittest
from pathlib import Path

import release_version as rv
import repo

CHROMIUM = "152.0.7977.149"


def as_tuple(version: str) -> tuple[int, ...]:
    return tuple(int(p) for p in version.split("."))


class ParseTagTest(unittest.TestCase):
    def test_respin_zero_has_no_suffix(self):
        self.assertEqual(rv.parse_tag("152.0.7977.149", CHROMIUM).version, "152.0.7977.14900")

    def test_respin_fills_the_last_two_digits(self):
        self.assertEqual(rv.parse_tag("152.0.7977.149-1", CHROMIUM).version, "152.0.7977.14901")
        self.assertEqual(rv.parse_tag("152.0.7977.149-99", CHROMIUM).version, "152.0.7977.14999")

    def test_releases_sort_below_the_next_upstream_release(self):
        versions = [rv.parse_tag("152.0.7977.149", CHROMIUM).version,
                    rv.parse_tag("152.0.7977.149-1", CHROMIUM).version,
                    rv.parse_tag("152.0.7977.160", "152.0.7977.160").version]
        tuples = [as_tuple(v) for v in versions]
        self.assertEqual(tuples, sorted(tuples))
        self.assertEqual(len(set(tuples)), 3)

    def test_rejects_another_chromium_release(self):
        with self.assertRaisesRegex(rv.ReleaseVersionError, "CHROMIUM_VERSION is 152.0.7977.149"):
            rv.parse_tag("152.0.7977.140-1", CHROMIUM)

    def test_rejects_a_spelled_out_respin_zero(self):
        for tag in ("152.0.7977.149-0", "152.0.7977.149-01"):
            with self.assertRaisesRegex(rv.ReleaseVersionError, "respin 0 has no suffix"):
                rv.parse_tag(tag, CHROMIUM)

    def test_rejects_malformed_tags(self):
        for tag in ("v152.0.7977.149", "152.0.7977", "152.0.7977.149-", "152.0.7977.149-a"):
            with self.assertRaises(rv.ReleaseVersionError, msg=tag):
                rv.parse_tag(tag, CHROMIUM)

    def test_rejects_a_hundredth_respin(self):
        with self.assertRaisesRegex(rv.ReleaseVersionError, "two digits"):
            rv.parse_tag("152.0.7977.149-100", CHROMIUM)

    def test_rejects_a_fourth_part_over_16_bits(self):
        self.assertEqual(rv.parse_tag("152.0.7977.655-35", "152.0.7977.655").version,
                         "152.0.7977.65535")
        for tag, chromium in (("152.0.7977.655-36", "152.0.7977.655"),
                              ("152.0.7977.656", "152.0.7977.656")):
            with self.assertRaisesRegex(rv.ReleaseVersionError, "65535", msg=tag):
                rv.parse_tag(tag, chromium)


class WriteTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.src = Path(tmp.name)
        (self.src / "chrome").mkdir()
        self.version_file = self.src / "chrome" / "VERSION"
        self.version_file.write_text("MAJOR=152\nMINOR=0\nBUILD=7977\nPATCH=149\n",
                                     encoding="utf-8", newline="\n")

    def test_writes_only_the_patch_line(self):
        rv.write_version_file(self.src, rv.parse_tag("152.0.7977.149-1", CHROMIUM))
        self.assertEqual(self.version_file.read_bytes(),
                         b"MAJOR=152\nMINOR=0\nBUILD=7977\nPATCH=14901\n")

    def test_refuses_to_write_twice(self):
        release = rv.parse_tag("152.0.7977.149-1", CHROMIUM)
        rv.write_version_file(self.src, release)
        with self.assertRaisesRegex(rv.ReleaseVersionError, "already written"):
            rv.write_version_file(self.src, release)

    def test_refuses_a_checkout_at_another_pin(self):
        self.version_file.write_text("MAJOR=152\nMINOR=0\nBUILD=7977\nPATCH=140\n",
                                     encoding="utf-8", newline="\n")
        with self.assertRaisesRegex(rv.ReleaseVersionError, "another pin"):
            rv.write_version_file(self.src, rv.parse_tag("152.0.7977.149-1", CHROMIUM))
        self.assertIn("PATCH=140", self.version_file.read_text(encoding="utf-8"))


class MainTest(unittest.TestCase):
    def test_compute_prints_the_release_version(self):
        pinned = repo.read_chromium_version()
        major_minor_build, patch = pinned.rsplit(".", 1)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = rv.main(["compute", "--tag", pinned + "-1"])
        self.assertEqual(code, 0)
        self.assertEqual(out.getvalue(), f"{major_minor_build}.{int(patch) * 100 + 1}\n")

    def test_errors_exit_1(self):
        with contextlib.redirect_stderr(io.StringIO()) as err:
            code = rv.main(["compute", "--tag", "1.2.3.4"])
        self.assertEqual(code, 1)
        self.assertIn("CHROMIUM_VERSION", err.getvalue())


if __name__ == "__main__":
    unittest.main()
