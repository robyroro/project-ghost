# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import mini_installer as mi


@unittest.skipUnless(sys.platform == "win32", "PE resources need Windows")
class ResourcesTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        self.exe = self.dir / "fixture.exe"
        shutil.copyfile(sys.executable, self.exe)
        self.pe = Path(sys.executable).read_bytes()
        mi.write_resources(self.exe, [mi.Resource("BD", "BASE.DLL", 1033, self.pe),
                                      mi.Resource("BN", "NOTES.TXT", 1033, b"not a PE")])

    def resources(self, exe):
        return {(r.type, r.name): r for r in mi.read_resources(exe)}

    def test_round_trip(self):
        found = self.resources(self.exe)
        self.assertEqual(found[("BD", "BASE.DLL")].data, self.pe)
        self.assertEqual(found[("BN", "NOTES.TXT")].language, 1033)

    def test_sign_signs_pe_resources_then_the_installer(self):
        calls = []

        def run(command, **kwargs):
            calls.append([str(c) for c in command])
            return subprocess.CompletedProcess(command, 0, "", "")

        out = self.dir / "signed.exe"
        before = self.exe.read_bytes()
        signed = mi.sign(self.exe, out, "AB" * 20, signtool=Path("signtool.exe"),
                         lzma=Path("7za.exe"), makecab=Path("makecab.py"),
                         work=self.dir / "work", run=run)
        self.assertEqual([p.name for p in signed], ["base.dll"])
        signtool = [c for c in calls if Path(c[0]).name == "signtool.exe"]
        self.assertEqual([Path(c[-1]).name for c in signtool], ["base.dll", "signed.exe"])
        found = self.resources(out)
        self.assertEqual(found[("BD", "BASE.DLL")].data, self.pe)
        self.assertEqual(found[("BN", "NOTES.TXT")].data, b"not a PE")
        self.assertEqual(self.exe.read_bytes(), before)  # the input is untouched

    def test_pe_files_inside(self):
        names = [p.name for p in mi.unpacked_pe_files(self.exe, self.dir / "x", Path("7za.exe"))]
        self.assertEqual(names, ["base.dll"])


if __name__ == "__main__":
    unittest.main()
