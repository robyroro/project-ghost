# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import argparse
import shutil
import tempfile
import unittest
from pathlib import Path

import ecdsa_p256 as ec
import sign_release as sr
import signing


class IdentityCheckTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        self.publisher = signing.scalar_signer("p", 2)
        self.pinned = signing.PinnedKeys(2, ec.spki(ec.public_key(9)),
                                         (self.publisher.key_hash, b"\x00" * 32))
        self.out = self.dir / "out"
        self.out.mkdir()

    def args(self, identity):
        (self.out / "args.gn").write_text(f'ghost_signing_identity = "{identity}"\n',
                                          encoding="utf-8")

    def test_matching(self):
        self.args("test")
        self.assertEqual(sr.check_identity("test", [self.out], self.publisher, self.pinned), [])

    def test_a_build_of_another_identity(self):
        self.args("dev")
        self.assertEqual(len(sr.check_identity("test", [self.out], None, self.pinned)), 1)

    def test_an_unpinned_publisher(self):
        self.args("test")
        problems = sr.check_identity("test", [self.out], signing.scalar_signer("x", 3),
                                     self.pinned)
        self.assertIn("is not pinned", problems[0])


class ArgumentsTest(unittest.TestCase):
    def ns(self, **kw):
        base = dict(crx=False, offline_installer=False, publisher_backup=None, version=None,
                    appid=None, updater_out=None)
        return argparse.Namespace(**{**base, **kw})

    def test_problems(self):
        self.assertIsNotNone(sr.argument_problem(self.ns()))
        self.assertIsNotNone(sr.argument_problem(self.ns(offline_installer=True)))
        self.assertIsNotNone(sr.argument_problem(self.ns(publisher_backup=Path("b"))))
        self.assertIsNone(sr.argument_problem(self.ns(crx=True)))
        self.assertIsNone(sr.argument_problem(self.ns(
            offline_installer=True, version="1.2.3.4", appid="{A}", updater_out=Path("u"))))


if __name__ == "__main__":
    unittest.main()
