# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import sys
import tempfile
import unittest
from pathlib import Path

import repo

sys.path.insert(0, str(repo.REPO_ROOT / "branding"))
import installer_strings  # noqa: E402

BRANDING = {"PRODUCT_FULLNAME": "Project Ghost", "COMPANY_FULLNAME": "Project Ghost"}
HEADER = "chrome/installer/util/installer_util_strings.h"

# The shape base/win/embedded_i18n/create_string_rc.py writes, with
# translations taken from 152.0.7977.140's generated file.
RC = "\n".join([
    '#include "installer_util_strings.h"',
    "",
    "STRINGTABLE",
    "BEGIN",
    '  IDS_ABOUT_VERSION_COMPANY_NAME_EN_US "The Chromium Authors"',
    '  IDS_ABOUT_VERSION_COMPANY_NAME_FR_CA "Les auteurs de Chrome"',
    '  IDS_INSTALL_FAILED_DE "Installation fehlgeschlagen. Wenn Chromium ausgeführt wird, '
    'schließen Sie es."',
    '  IDS_INSTALL_FAILED_EN_GB "Installation failed. If Google Chrome is currently running, '
    'please close it."',
    '  IDS_INSTALL_FAILED_EN_US "Installation failed. If Chromium is currently running, '
    'please close it."',
    '  IDS_INSTALL_FAILED_HI "इंस्‍टॉलेशन विफल हुआ. अगर क्रोमियम अभी खुला हुआ है, तो कृपया उसे बंद करें."',
    '  IDS_INSTALL_FAILED_KM "ការដំឡើងបានបរាជ័យ។ សូមទាញយក Google Chromium ម្តងទៀត។"',
    '  IDS_PRODUCT_DESCRIPTION_EN_US "Chromium is a ""fast"" browser."',
    '  IDS_PRODUCT_NAME_EN_US "Chromium"',
    '  IDS_PRODUCT_NAME_JA "Chromium"',
    '  IDS_SHORTCUT_TOOLTIP_DE "Zugriff auf das Internet"',
    # Constructed: fr-CA names Chrome in other 152 strings.
    '  IDS_SHORTCUT_TOOLTIP_FR_CA "Accéder à Internet avec Chrome"',
    '  IDS_SHORTCUT_TOOLTIP_EN_US "Access the Internet"',
    "END",
    ""])


def entries(rc_text):
    return dict(installer_strings.parse(rc_text))


class BrandTest(unittest.TestCase):
    def setUp(self):
        self.branded = installer_strings.brand(RC, BRANDING, HEADER)
        self.values = entries(self.branded)

    def test_names_are_the_branding_values_in_every_language(self):
        # Upstream translates the company name, and fr-CA's names Chrome.
        for key in ("IDS_PRODUCT_NAME_EN_US", "IDS_PRODUCT_NAME_JA",
                    "IDS_ABOUT_VERSION_COMPANY_NAME_EN_US", "IDS_ABOUT_VERSION_COMPANY_NAME_FR_CA"):
            self.assertEqual(self.values[key], "Project Ghost", key)

    def test_sentences_name_the_product(self):
        self.assertEqual(self.values["IDS_INSTALL_FAILED_DE"],
                         "Installation fehlgeschlagen. Wenn Project Ghost ausgeführt wird, "
                         "schließen Sie es.")
        self.assertEqual(self.values["IDS_INSTALL_FAILED_EN_GB"],
                         "Installation failed. If Project Ghost is currently running, "
                         "please close it.")
        # km and or call it Google Chromium.
        self.assertEqual(self.values["IDS_INSTALL_FAILED_KM"],
                         "ការដំឡើងបានបរាជ័យ។ សូមទាញយក Project Ghost ម្តងទៀត។")

    def test_a_translation_that_loses_the_name_falls_back_to_english(self):
        # hi transliterates Chromium, so no substitution can name the product.
        self.assertEqual(self.values["IDS_INSTALL_FAILED_HI"],
                         self.values["IDS_INSTALL_FAILED_EN_US"])

    def test_a_translation_naming_another_browser_falls_back_to_english(self):
        # en-US names no product here, so only the upstream names give it away.
        self.assertEqual(self.values["IDS_SHORTCUT_TOOLTIP_FR_CA"], "Access the Internet")

    def test_strings_without_a_name_are_unchanged(self):
        self.assertEqual(self.values["IDS_SHORTCUT_TOOLTIP_DE"], "Zugriff auf das Internet")

    def test_no_upstream_name_survives(self):
        for key, value in self.values.items():
            for name in ("Chromium", "Chrome", "Google"):
                self.assertNotIn(name, value, key)

    def test_rc_escaping_is_kept(self):
        self.assertIn('  IDS_PRODUCT_DESCRIPTION_EN_US "Project Ghost is a ""fast"" browser."',
                      self.branded.splitlines())

    def test_includes_the_header_by_its_path_from_the_gen_root(self):
        self.assertEqual(self.branded.splitlines()[0], f'#include "{HEADER}"')
        self.assertEqual(self.branded.splitlines()[1:4], ["", "STRINGTABLE", "BEGIN"])

    def test_every_entry_is_kept_in_order(self):
        self.assertEqual([k for k, _ in installer_strings.parse(self.branded)],
                         [k for k, _ in installer_strings.parse(RC)])


class FileTest(unittest.TestCase):
    def test_reads_the_committed_branding_file(self):
        branding = installer_strings.read_branding(repo.REPO_ROOT / "branding" / "BRANDING")
        self.assertEqual(branding["PRODUCT_FULLNAME"], "Project Ghost")
        self.assertEqual(branding["COMPANY_FULLNAME"], "Project Ghost")

    def test_rewrites_the_utf16_file_the_rc_compiler_reads(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, output = Path(tmp) / "in.rc", Path(tmp) / "out.rc"
            source.write_text(RC, encoding="utf-16", newline="\n")
            installer_strings.main(["--branding", str(repo.REPO_ROOT / "branding" / "BRANDING"),
                                    "--header", HEADER, str(source), str(output)])
            raw = output.read_bytes()
        self.assertEqual(raw[:2], b"\xff\xfe")
        self.assertEqual(entries(raw.decode("utf-16"))["IDS_PRODUCT_NAME_JA"], "Project Ghost")
        self.assertNotIn(b"\r\n", raw)


if __name__ == "__main__":
    unittest.main()
