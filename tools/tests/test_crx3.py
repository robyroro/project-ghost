# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import hashlib
import io
import unittest
import zipfile

import crx3
import ecdsa_p256 as ec

KEY = 0xC9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721


class Crx3Test(unittest.TestCase):
    def setUp(self):
        self.data = crx3.build({"mini_installer.exe": b"MZ installer"}, KEY)

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
        self.assertEqual(crx3.build({"mini_installer.exe": b"MZ installer"}, KEY), self.data)

    def test_rejects_other_formats(self):
        with self.assertRaises(ValueError):
            crx3.parse(b"PK\x03\x04")


if __name__ == "__main__":
    unittest.main()
