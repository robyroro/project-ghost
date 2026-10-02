# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest import mock

import offline_installer as oi


class ManifestTest(unittest.TestCase):
    def test_manifest_runs_the_browser_installer_per_user(self):
        root = ET.fromstring(oi.MANIFEST)
        action = root.find("./app/updatecheck/manifest/actions/action[@event='install']")
        self.assertEqual(action.get("run"), "${INSTALLER_FILENAME}")
        self.assertEqual(action.get("needsadmin"), "false")
        self.assertIn("--do-not-launch-chrome", action.get("arguments"))
        self.assertEqual(root.find("./app").get("appid"), "${APP_ID}")


class CommandTest(unittest.TestCase):
    def test_sign_py_command(self):
        out = Path(r"C:\src\out\vanilla")
        argv = oi.sign_command(Path(r"C:\depot_tools"), out, Path(r"C:\x\mini_installer.exe"),
                               Path(r"C:\x\manifest.gup"), "{APPID}", "152.0.7977.14901",
                               Path(r"C:\x\Setup.exe"))
        self.assertEqual(Path(argv[0]).name, "vpython3.bat")
        self.assertEqual(argv[1], str(out / "UpdaterSigning" / "sign.py"))
        self.assertIn("--disable_tag_and_sign", argv)
        self.assertEqual(argv[argv.index("--appid") + 1], "{APPID}")
        self.assertEqual(argv[argv.index("--lzma_7z") + 1],
                         str(out / "UpdaterSigning" / "7zr.exe"))
        self.assertIn("'${INSTALLER_VERSION}': '152.0.7977.14901'",
                      argv[argv.index("--manifest_dict_replacements") + 1])

    def test_browser_installer_may_come_from_another_output_dir(self):
        # The metainstaller must be a static build (out/updater); the browser
        # installer may come from the component build.
        with mock.patch.object(oi.subprocess, "run") as run:
            run.return_value.returncode = 0
            oi.main(["--src", r"C:\src", "--out", r"out\updater", "--version", "1.2.3.4",
                     "--appid", "{A}", "--output", r"C:\x\Setup.exe",
                     "--installer", r"C:\src\out\vanilla\mini_installer.exe"])
        argv = run.call_args.args[0]
        self.assertEqual(argv[argv.index("--installer_path") + 1],
                         str(Path(r"C:\src\out\vanilla\mini_installer.exe").resolve()))
        self.assertEqual(argv[argv.index("--in_file") + 1],
                         str(Path(r"C:\src\out\updater").resolve() / "UpdaterSetup.exe"))

    def test_install_arguments(self):
        self.assertEqual(oi.install_arguments("{APPID}"),
                         ["--install=appguid={APPID}&appname=Project%20Ghost&needsadmin=False",
                          "--silent"])


if __name__ == "__main__":
    unittest.main()
