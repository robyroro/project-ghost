# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import hashlib
import io
import unittest
import zipfile

import crx3
import ecdsa_p256 as ec
import signing

KEY = 0xC9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721
DEVELOPER = signing.scalar_signer("developer", KEY)
PUBLISHER = signing.scalar_signer("publisher", 0x1234567)
FILES = {"mini_installer.exe": b"MZ installer"}


class Crx3Test(unittest.TestCase):
    def setUp(self):
        self.data = crx3.build(FILES, DEVELOPER)

    def test_layout(self):
        self.assertEqual(self.data[:4], b"Cr24")
        self.assertEqual(int.from_bytes(self.data[4:8], "little"), 3)
        package = crx3.parse(self.data)
        with zipfile.ZipFile(io.BytesIO(package.archive)) as archive:
            self.assertEqual(archive.read("mini_installer.exe"), b"MZ installer")

    def test_signed_data_names_the_key(self):
        package = crx3.parse(self.data)
        public = ec.spki(ec.public_key(KEY))
        self.assertEqual(package.crx_id, hashlib.sha256(public).digest()[:16])
        self.assertEqual([key for key, _ in package.proofs], [public])

    def test_proofs_verify(self):
        self.assertEqual(crx3.verified_keys(self.data), [ec.spki(ec.public_key(KEY))])

    def test_tampering_breaks_the_proof(self):
        tampered = self.data[:-1] + bytes([self.data[-1] ^ 1])
        self.assertEqual(crx3.verified_keys(tampered), [])

    def test_builds_are_deterministic(self):
        self.assertEqual(crx3.build(FILES, DEVELOPER), self.data)

    def test_publisher_proofs(self):
        data = crx3.build(FILES, DEVELOPER, [PUBLISHER])
        self.assertEqual(crx3.parse(data).crx_id, crx3.crx_id(DEVELOPER.public_der))
        self.assertEqual(crx3.verified_keys(data),
                         [DEVELOPER.public_der, PUBLISHER.public_der])

    def test_a_developer_key_that_is_also_the_publisher_signs_once(self):
        self.assertEqual(crx3.parse(crx3.build(FILES, DEVELOPER, [DEVELOPER])).proofs,
                         crx3.parse(self.data).proofs)

    def test_rejects_other_formats(self):
        with self.assertRaises(ValueError):
            crx3.parse(b"PK\x03\x04")


if __name__ == "__main__":
    unittest.main()
