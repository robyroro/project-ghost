# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The breakage issue form has exactly the fields Shade fills in (Phase 4D-2)."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FORM = ROOT / ".github" / "ISSUE_TEMPLATE" / "breakage.yml"
REPORT = ROOT / "browser" / "protections" / "breakage_report.cc"
REPORT_H = ROOT / "browser" / "protections" / "breakage_report.h"
STRINGS = ROOT / "browser" / "ui" / "protections" / "protections_strings.cc"
CATEGORIES = ("Page content missing or broken", "Video or audio", "Sign-in or forms",
              "Something else")


def form_ids() -> list[str]:
    return re.findall(r"^\s+id:\s*(\w+)\s*$", FORM.read_text(encoding="utf-8"), re.M)


def written_ids() -> set[str]:
    # BuildReport's fields, then the three the panel fills in.
    ids = set(re.findall(r'^\s+\{"(\w+)", "[^"]*", ', REPORT.read_text(encoding="utf-8"), re.M))
    ids |= set(re.findall(r'FieldId\[\] = "(\w+)"', REPORT_H.read_text(encoding="utf-8")))
    return ids


class BreakageFormTest(unittest.TestCase):
    def test_the_form_has_the_fields_shade_writes(self):
        self.assertEqual(sorted(form_ids()), sorted(written_ids()))
        self.assertEqual(len(form_ids()), len(set(form_ids())))

    def test_every_field_can_be_filled_from_the_url(self):
        text = FORM.read_text(encoding="utf-8")
        self.assertNotIn("type: dropdown", text)
        self.assertNotIn("type: checkboxes", text)

    def test_the_form_labels_its_issues(self):
        self.assertRegex(FORM.read_text(encoding="utf-8"), r"(?m)^labels:\s*\[\s*breakage\s*\]")

    def test_the_categories_are_the_panels(self):
        labels = re.findall(r'return "([^"]+)";', REPORT.read_text(encoding="utf-8"))
        strings = STRINGS.read_text(encoding="utf-8")
        form = FORM.read_text(encoding="utf-8")
        for name in CATEGORIES:
            self.assertIn(name, labels)
            self.assertIn(f'"{name}"', strings)
            self.assertIn(name, form)


if __name__ == "__main__":
    unittest.main()
