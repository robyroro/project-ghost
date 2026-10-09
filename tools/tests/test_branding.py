# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The product's names in branding/: final, and consistent between the
browser and the updater."""

import re
import unittest

import repo

BRANDING = repo.REPO_ROOT / "branding"
# The working name and every identifier derived from it. The codename
# "ghost" (//ghost, GHOST_INDEX, comments) is internal and allowed.
WORKING_NAME = re.compile(r"project ?ghost|GhostHTM|GhostPDF|pgupdate|PGst", re.IGNORECASE)


def _install_modes_field(pattern: str) -> str:
    text = (BRANDING / "install_modes.h").read_text(encoding="utf-8")
    return re.search(pattern, text).group(1)


class BrandingTest(unittest.TestCase):
    def test_no_working_name_is_left(self):
        for name in ("BRANDING", "install_modes.h", "updater.gni"):
            lines = (BRANDING / name).read_text(encoding="utf-8").splitlines()
            for number, line in enumerate(lines, 1):
                self.assertIsNone(WORKING_NAME.search(line), f"branding/{name}:{number}: {line}")

    def test_the_updater_lives_in_the_browsers_company_directory(self):
        self.assertEqual(
            repo.read_gni_string(BRANDING / "updater.gni", "updater_company_short_name"),
            _install_modes_field(r'kCompanyPathName\[\]\s*=\s*L"([^"]*)"'))

    def test_the_updater_finds_the_browser_by_its_app_id(self):
        self.assertEqual(repo.read_gni_string(BRANDING / "updater.gni", "browser_appid"),
                         _install_modes_field(r'\.app_guid\s*=\s*L"([^"]*)"'))


if __name__ == "__main__":
    unittest.main()
