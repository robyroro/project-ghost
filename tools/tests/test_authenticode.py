# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import hashlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import authenticode as ac
import signing

THUMB = "AB" * 20


class Runner:
    """A fake subprocess.run: fails for the services in `failing`."""

    def __init__(self, failing=()):
        self.calls, self.failing = [], set(failing)

    def __call__(self, command, **kwargs):
        self.calls.append([str(c) for c in command])
        service = command[command.index("/tr") + 1] if "/tr" in command else None
        failed = service in self.failing
        return subprocess.CompletedProcess(command, 1 if failed else 0, "",
                                           "SignTool Error: timestamp" if failed else "")


class SignTest(unittest.TestCase):
    def test_command(self):
        self.assertEqual(ac.sign_command(Path("signtool.exe"), THUMB, "http://ts",
                                         [Path("a.dll")]),
                         ["signtool.exe", "sign", "/q", "/fd", "SHA256", "/sha1", THUMB,
                          "/tr", "http://ts", "/td", "SHA256", "a.dll"])

    def test_batches(self):
        run = Runner()
        files = [Path(f"f{i}.dll") for i in range(ac.BATCH + 1)]
        ac.sign(files, THUMB, Path("signtool.exe"), run)
        self.assertEqual([len(c) - 11 for c in run.calls], [ac.BATCH, 1])

    def test_the_second_timestamp_service_is_tried(self):
        run = Runner(failing={ac.TIMESTAMP_SERVICES[0]})
        ac.sign([Path("a.dll")], THUMB, Path("signtool.exe"), run)
        self.assertEqual([c[c.index("/tr") + 1] for c in run.calls], list(ac.TIMESTAMP_SERVICES))

    def test_every_service_failing_stops(self):
        with self.assertRaises(signing.SigningError):
            ac.sign([Path("a.dll")], THUMB, Path("signtool.exe"),
                    Runner(failing=ac.TIMESTAMP_SERVICES))


class FilesTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)

    def test_pe_files(self):
        (self.dir / "sub").mkdir()
        (self.dir / "sub" / "a.dll").write_bytes(b"MZ\x90\x00")
        (self.dir / "b.exe").write_bytes(b"MZ")
        (self.dir / "c.dll").write_bytes(b"not a PE")
        (self.dir / "d.pak").write_bytes(b"MZ")
        self.assertEqual([p.relative_to(self.dir).as_posix() for p in ac.pe_files(self.dir)],
                         ["b.exe", "sub/a.dll"])

    def test_newest_signtool(self):
        for version in ("10.0.17134.0", "10.0.26100.0", "10.0.9200.0"):
            tool = self.dir / version / "x64" / "signtool.exe"
            tool.parent.mkdir(parents=True)
            tool.write_bytes(b"MZ")
        self.assertEqual(ac.signtool_path(self.dir).parent.parent.name, "10.0.26100.0")

    def test_no_signtool(self):
        with self.assertRaises(signing.SigningError):
            ac.signtool_path(self.dir)

    def test_thumbprint(self):
        self.assertEqual(ac.thumbprint(b"cert"), hashlib.sha1(b"cert").hexdigest().upper())


@unittest.skipUnless(sys.platform == "win32", "Authenticode needs Windows")
class VerifyTest(unittest.TestCase):
    def test_a_file_not_signed_by_the_certificate_fails(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        copy = Path(shutil.copy(sys.executable, tmp / "python.exe"))
        failures = ac.verify([copy], "0" * 40, require_trusted=False)
        self.assertEqual(len(failures), 1)
        self.assertIn("python.exe: signed by", failures[0])


if __name__ == "__main__":
    unittest.main()
