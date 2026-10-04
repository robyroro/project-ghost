# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import hashlib
import unittest

import ecdsa_p256 as ec

# RFC 6979, appendix A.2.5: P-256 with SHA-256.
RFC_KEY = 0xC9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721
RFC_PUBLIC = (0x60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6,
              0x7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299)
RFC_SIGNATURES = {
    b"sample": (0xEFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716,
                0xF7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8),
    b"test": (0xF1ABB023518351CD71D881567B1EA663ED3EFCF6C5132B354F28D3B0B7D38367,
              0x019F4113742A2B14BD25926B49C649155F267E60D3814B4C0CC84250E46F0083),
}

# components/update_client/request_sender.cc at 152.0.7977.149: Google's CUP key.
UPSTREAM_CUP_SPKI = bytes.fromhex(
    "3059301306072a8648ce3d020106082a8648ce3d030107034200045195"
    "3bf48cd41b718d23fd5456b618f0d06556e856e78f8d1518406bf3565b"
    "c9fe1ede2da50adf0781865d4646c94821e25c5e601b8a79f19fd3dcaf"
    "aa5aaac4")


class EcdsaTest(unittest.TestCase):
    def test_public_key_matches_rfc6979(self):
        self.assertEqual(ec.public_key(RFC_KEY), RFC_PUBLIC)

    def test_signatures_match_rfc6979(self):
        for message, signature in RFC_SIGNATURES.items():
            self.assertEqual(ec.sign(RFC_KEY, message), signature, message)

    def test_nonce_reduces_the_digest_mod_n(self):
        # RFC 6979's bits2octets reduces the digest mod N. Real SHA-256 digests
        # reach N with probability about 2^-32, so construct one that does.
        self.assertEqual(ec._rfc6979_nonce(RFC_KEY, (ec.N + 1).to_bytes(32, "big")),
                         ec._rfc6979_nonce(RFC_KEY, (1).to_bytes(32, "big")))

    def test_verify_accepts_and_rejects(self):
        r, s = ec.sign(RFC_KEY, b"sample")
        self.assertTrue(ec.verify(RFC_PUBLIC, b"sample", r, s))
        self.assertFalse(ec.verify(RFC_PUBLIC, b"samplf", r, s))
        self.assertFalse(ec.verify(RFC_PUBLIC, b"sample", r, (s + 1) % ec.N))
        self.assertFalse(ec.verify(RFC_PUBLIC, b"sample", 0, s))

    def test_der_signature_round_trips_with_minimal_integers(self):
        for r, s in RFC_SIGNATURES.values():
            der = ec.der_signature(r, s)
            self.assertEqual(ec.parse_der_signature(der), (r, s))
            self.assertLessEqual(len(der), 72)  # cup.cc accepts 8 to 72 bytes
        # A high first byte needs a leading zero; a low one doesn't.
        self.assertEqual(ec.der_signature(0x80, 0x7F), bytes.fromhex("30070202008002017f"))

    def test_spki_matches_upstreams_encoding(self):
        point = ec.parse_spki(UPSTREAM_CUP_SPKI)
        self.assertTrue(ec.on_curve(point))
        self.assertEqual(ec.spki(point), UPSTREAM_CUP_SPKI)

    def test_generated_keys_sign_and_verify(self):
        key = ec.generate_private_key()
        public = ec.public_key(key)
        self.assertTrue(ec.on_curve(public))
        self.assertTrue(ec.verify(public, b"m", *ec.sign(key, b"m")))


class DigestTest(unittest.TestCase):
    def test_signing_a_digest_equals_signing_the_message(self):
        d = ec.generate_private_key()
        self.assertEqual(ec.sign_digest(d, hashlib.sha256(b"sample").digest()),
                         ec.sign(d, b"sample"))

    def test_a_digest_is_32_bytes(self):
        with self.assertRaises(ValueError):
            ec.sign_digest(1, b"short")

    def test_raw_signatures_convert_to_der(self):
        d = ec.generate_private_key()
        r, s = ec.sign(d, b"sample")
        raw = r.to_bytes(32, "big") + s.to_bytes(32, "big")
        self.assertEqual(ec.der_from_raw(raw), ec.der_signature(r, s))
        with self.assertRaises(ValueError):
            ec.der_from_raw(raw[:63])


if __name__ == "__main__":
    unittest.main()
