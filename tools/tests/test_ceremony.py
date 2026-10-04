# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import datetime
import json
import shutil
import tempfile
import unittest
from pathlib import Path

import ceremony
import crx3
import ecdsa_p256 as ec
import signing
import update_server as us

PASSWORD = b"correct horse battery staple"
IDENTITY = ceremony.IDENTITIES["test"]


class FakeCustody:
    def __init__(self):
        self.keys, self.certificates, self.pins = {}, {}, {}

    def publisher_exists(self, name):
        return name in self.keys

    def create_publisher(self, name, pin):
        self.keys[name] = signing.scalar_signer(name, 0xA11CE)
        self.pins[name] = pin
        return self.keys[name]

    def codesign_exists(self, subject):
        return subject in self.certificates

    def create_codesign(self, subject):
        self.certificates[subject] = b"0\x82 a fake certificate"
        return self.certificates[subject]


class CeremonyTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root)
        self.custody = FakeCustody()
        self.backup = self.root / "stick" / "backup.p8"
        self.backup.parent.mkdir()
        self.cup = self.root / "keys" / "cup_key_2.json"

    def run_ceremony(self):
        return ceremony.run(IDENTITY, self.custody, self.backup, PASSWORD, self.cup, pin=True,
                            root=self.root, today=datetime.date(2026, 10, 5), commit="abc123")

    def test_outputs(self):
        record = self.run_ceremony()
        primary = self.custody.keys[IDENTITY.publisher_key]
        backup = signing.backup_signer(self.backup, PASSWORD)
        cup = signing.file_signer(self.cup)
        pinned = signing.pinned_keys("test", self.root / "branding" / "keys")
        self.assertEqual(pinned, signing.PinnedKeys(2, cup.public_der,
                                                    (primary.key_hash, backup.key_hash)))
        self.assertEqual((self.root / "branding/signing/test_codesign.cer").read_bytes(),
                         b"0\x82 a fake certificate")
        developer = signing.file_signer(us.CRX_KEY_FILE).public_der
        fixtures = self.root / "test/updater/data/test_identity"
        self.assertEqual(crx3.verified_keys((fixtures / "ghost_publisher.crx3").read_bytes()),
                         [developer, primary.public_der])
        self.assertEqual(crx3.verified_keys(
            (fixtures / "ghost_backup_publisher.crx3").read_bytes()),
            [developer, backup.public_der])
        vector = json.loads((self.root / "test/updater/cup_vector_test_identity.json")
                            .read_text(encoding="utf-8"))
        self.assertEqual(vector["cup2key"], "2:12345")
        self.assertTrue(us.cup_verify(ec.parse_spki(cup.public_der), vector["cup2key"],
                                      vector["request"].encode(), vector["response"].encode(),
                                      vector["proof"]))
        self.assertEqual(record.name, "2026-10-05-test-identity.md")
        text = record.read_text(encoding="utf-8")
        for expected in (primary.key_hash.hex(), backup.key_hash.hex(), cup.key_hash.hex(),
                         "abc123", "backup.p8", "PIN on each use: yes"):
            self.assertIn(expected, text)
        self.assertTrue(self.custody.pins[IDENTITY.publisher_key])

    def test_a_second_run_refuses_and_changes_nothing(self):
        self.run_ceremony()
        before = {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        with self.assertRaises(ceremony.CeremonyError):
            self.run_ceremony()
        self.assertEqual({p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()},
                         before)

    def test_an_existing_tpm_key_refuses(self):
        self.custody.keys[IDENTITY.publisher_key] = object()
        with self.assertRaises(ceremony.CeremonyError):
            self.run_ceremony()
        self.assertFalse(self.backup.exists())


if __name__ == "__main__":
    unittest.main()
