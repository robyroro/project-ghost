# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import os
import secrets
import sys
import unittest

import ecdsa_p256 as ec
import tpm


class BlobTest(unittest.TestCase):
    def test_public_key_blob(self):
        x, y = ec.public_key(12345)
        blob = (0x31534345).to_bytes(4, "little") + (32).to_bytes(4, "little") \
            + x.to_bytes(32, "big") + y.to_bytes(32, "big")
        self.assertEqual(tpm.public_der_from_blob(blob), ec.spki((x, y)))

    def test_other_blobs_are_refused(self):
        with self.assertRaises(ValueError):
            tpm.public_der_from_blob(b"\x00" * 72)


@unittest.skipUnless(sys.platform == "win32" and os.environ.get("GHOST_TPM_TESTS") == "1",
                     "uses this PC's TPM; set GHOST_TPM_TESTS=1 to run")
class TpmTest(unittest.TestCase):
    def test_create_sign_delete(self):
        name = "ProjectGhost-unittest-" + secrets.token_hex(4)
        public = tpm.create_key(name, pin_protected=False)
        try:
            self.assertTrue(tpm.key_exists(name))
            signer = tpm.open_signer(name)
            self.assertEqual(signer.public_der, public)
            signature = signer.sign(b"message")
            self.assertTrue(ec.verify(ec.parse_spki(public), b"message",
                                      *ec.parse_der_signature(signature)))
            with self.assertRaises(tpm.TpmError):
                tpm.create_key(name, pin_protected=False)
        finally:
            tpm.delete_key(name)
        self.assertFalse(tpm.key_exists(name))


if __name__ == "__main__":
    unittest.main()
