# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import hashlib
import shutil
import tempfile
import unittest
from pathlib import Path

import ecdsa_p256 as ec
import lint
import signing
import update_server as us

# RFC 6979's P-256 test key: public on purpose.
KEY = 0xC9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721


def spki(d: int) -> bytes:
    return ec.spki(ec.public_key(d))


class TempDir(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)


class SignerTest(unittest.TestCase):
    def test_a_scalar_signer_signs_verifiably(self):
        signer = signing.scalar_signer("k", KEY)
        self.assertEqual(signer.public_der, spki(KEY))
        signature = signer.sign(b"message")
        self.assertTrue(ec.verify(ec.public_key(KEY), b"message",
                                  *ec.parse_der_signature(signature)))
        self.assertEqual(signer.key_hash, hashlib.sha256(spki(KEY)).digest())

    def test_a_file_signer_reads_a_committed_key(self):
        signer = signing.file_signer(us.CRX_KEY_FILE)
        self.assertEqual(signer.public_der, spki(signing.load_key_file(us.CRX_KEY_FILE)))


class KeyFileTest(TempDir):
    def test_write_then_load(self):
        path = self.dir / "k.json"
        signing.write_key_file(path, KEY, "a test key")
        self.assertEqual(signing.load_key_file(path), KEY)
        self.assertIn('"comment": "a test key"', path.read_text(encoding="utf-8"))


class BackupTest(TempDir):
    def test_round_trip(self):
        path = self.dir / "backup.p8"
        signing.write_backup(path, KEY, b"correct horse battery")
        self.assertIn(b"-----BEGIN ENCRYPTED " + b"PRIVATE KEY-----", path.read_bytes())
        self.assertEqual(signing.read_backup(path, b"correct horse battery"), KEY)
        self.assertEqual(signing.backup_signer(path, b"correct horse battery").public_der,
                         spki(KEY))

    def test_a_wrong_password_is_refused_without_the_key(self):
        path = self.dir / "backup.p8"
        signing.write_backup(path, KEY, b"correct horse battery")
        with self.assertRaises(signing.SigningError) as caught:
            signing.read_backup(path, b"wrong")
        self.assertNotIn(f"{KEY:x}", str(caught.exception))


class HeaderTest(TempDir):
    def test_render_then_read(self):
        cup, primary, backup = spki(KEY), spki(2), spki(3)
        text = signing.render_identity_header("dev", 1, cup, [primary, backup], "a test")
        self.assertTrue(lint.has_mpl_notice(text))
        (self.dir / "dev.h").write_text(text, encoding="utf-8")
        self.assertEqual(signing.pinned_keys("dev", self.dir), signing.PinnedKeys(
            1, cup, (hashlib.sha256(primary).digest(), hashlib.sha256(backup).digest())))

    def test_an_identity_pins_two_publisher_keys(self):
        with self.assertRaises(ValueError):
            signing.render_identity_header("dev", 1, spki(KEY), [spki(2)], "a test")


class BuildIdentityTest(TempDir):
    def test_default_then_set(self):
        args = self.dir / "args.gn"
        args.write_text('import("//ghost/build/args/dev.gn")\n', encoding="utf-8")
        self.assertEqual(signing.build_identity(self.dir), "dev")
        args.write_text('import("//ghost/build/args/dev.gn")\n'
                        'ghost_signing_identity = "test"\n', encoding="utf-8")
        self.assertEqual(signing.build_identity(self.dir), "test")


if __name__ == "__main__":
    unittest.main()
