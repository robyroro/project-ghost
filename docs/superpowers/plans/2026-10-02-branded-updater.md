# Branded Updater Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ghost installs, updates and uninstalls per-user through a Ghost-branded `//chrome/updater` under the test identity, and an update from respin `-1` to `-2` is applied end to end in Windows Sandbox from a CUP-signed response and a CRX3 carrying Ghost's publisher proof, with a request that holds only allow-listed keys.

**Architecture:**
- **Branding.** `//ghost/branding/updater.gni` replaces the updater's Chromium branding through a one-line hook in `chrome/updater/branding.gni`.
- **Layout.** The browser moves under a company directory (`ProjectGhost\Browser`) and registers with the updater under `Software\ProjectGhost\Update`. The company directory was first written with a space; the end-to-end test showed upstream's updater uninstall can't handle one.
- **Trust.**
  - CUP is pinned to ECDSA with Ghost's key.
  - The updater requires a new `CRX3_WITH_GHOST_PUBLISHER_PROOF` format.
  - An allow-list scrubber cleans the serialized request, and event requests are not sent.
- **Test side.** Stdlib Python tools provide P-256 ECDSA, CRX3 packing, an Omaha 4 test server, the offline installer and the Sandbox end-to-end test.

**Tech Stack:** Python 3 stdlib (`tools/`, `unittest`), GN, C++ (Chromium `base`, `components/update_client`, `components/crx_file`, gtest), the patch series, `vpython3` (for upstream's `sign.py`), Windows Sandbox.

**Spec:** [docs/superpowers/specs/2026-10-02-branded-updater-design.md](../specs/2026-10-02-branded-updater-design.md)

---

## Conventions for every task

Everything in sub-project A's plan ([2026-10-02-release-version.md](2026-10-02-release-version.md#conventions-for-every-task)) applies. In short:

- **Paths.** `WEBOPS=/c/Users/robyv/Desktop/DLU/webops`, `SRC=$WEBOPS/chromium/src`.
- **Build environment.** `export PATH="/c/src/depot_tools:$PATH" DEPOT_TOOLS_WIN_TOOLCHAIN=0`.
- **`//ghost` changes.**
  1. Commit in `WEBOPS`.
  2. `git -C $SRC/ghost pull --ff-only`.
  3. Build.
- **Chromium changes.**
  1. Edit in `$SRC`.
  2. `git -C $SRC commit -as` with a message whose body ends with `Why:` and `Upstream:` trailers.
  3. `python tools/patches.py export --src chromium/src`.
  4. Commit `patches/`.
- **Before each commit in `WEBOPS`:**

  ```bash
  git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools
  ```
- **Commits.** No `Co-Authored-By` or "Generated with" lines.
- **Never:**
  - rename `out\` directories;
  - edit Chromium sources while a build runs;
  - re-run `patches.py apply`;
  - run an installer on this machine. Installers run only in Windows Sandbox.
- **Builds over a few minutes** run in the background, with their time and action count noted for the spike notes.

**Identity values used throughout** (test identity, from the spec):

| Value | |
|---|---|
| Company (`kCompanyPathName`, updater company short name) | `Project Ghost` |
| Browser product (`kProductPathName`) | `Browser` |
| Updater product name | `ProjectGhostUpdater` (display `Project Ghost Updater`) |
| Update URL | `http://127.0.0.1:8484/update` |
| Browser app ID | generated in Task 6; written below as `{BROWSER_APPID}` |

---

## File map

| File | Responsibility |
|---|---|
| `tools/ecdsa_p256.py` | P-256 ECDSA-SHA256 with RFC 6979 nonces, DER signatures, SubjectPublicKeyInfo |
| `tools/crx3.py` | Build, parse and verify CRX3 packages |
| `tools/update_server.py` | Test keys and their C++ headers, CUP proofs, the allow-list checker, Omaha 4 responses, the HTTP server |
| `tools/offline_installer.py` | The offline metainstaller, through upstream `sign.py` |
| `tools/update_smoke.py` | The end-to-end test in Windows Sandbox |
| `tools/installer_smoke.py` | Learns the company directory; `sandbox_config` accepts other scripts |
| `tools/tests/test_ecdsa_p256.py`, `test_crx3.py`, `test_update_server.py`, `test_offline_installer.py`, `test_update_smoke.py` | Their tests |
| `test/updater/cup_test_key.json`, `crx_test_key.json` | Test-identity private keys |
| `test/updater/cup_vector.json` | A CUP request, response and proof that Chromium's verifier must accept |
| `test/updater/request_all_fields.json`, `request_scrubbed.json` | The scrubber's golden input and output, shared by the C++ and Python tests |
| `test/updater/data/ghost_publisher.crx3`, `other_publisher.crx3` | CRX3 fixtures for the verifier test |
| `branding/install_modes.h` | Company and product paths; the browser's `app_guid` |
| `branding/updater.gni` | The updater identity (generated once, in Task 6) |
| `branding/cup_key.h`, `branding/crx_publisher_key.h` | Generated from the test keys by `update_server.py keygen` |
| `branding/BUILD.gn` | `//ghost/branding:keys` (the two headers) |
| `components/update_client/request_scrubber.{h,cc}`, `BUILD.gn` | The allow-list scrubber |
| `components/update_client/request_scrubber_unittest.cc` | Golden test |
| `updater/crx_verifier_unittest.cc`, `updater/cup_unittest.cc` | The verifier format; CUP interoperability with the Python signer |
| `build/args/dev.gn` | `enable_updater`, `enable_update_notifications` |
| `BUILD.gn` | New test sources and data |
| `patches/0015`–`0022` | Hooks, listed in the tasks |
| `docs/superpowers/specs/2026-10-02-branded-updater-spike.md` | Spike results and the audit |
| `docs/privacy-model.md`, `docs/architecture.md`, `docs/build/windows.md`, `docs/roadmap.md` | Documentation |

---

### Task 0: Preconditions

- [ ] **Step 1: Check the state.**

```bash
cd $WEBOPS && git status -sb | head -1
git -C $SRC status --porcelain --untracked-files=no | wc -l
git -C $SRC rev-parse --abbrev-ref HEAD
git -C $SRC/ghost status -sb | head -1
df -h /c | tail -1
python tools/patches.py check | tail -1
```

Expected:
- `main` on `origin/main`, or ahead only by this plan;
- `0`;
- `ghost/152.0.7977.149`;
- `$SRC/ghost` on `main`;
- at least 80 GB free (the updater targets and two respin builds);
- `14 patch(es) checked, 0 error(s).`

---

### Task 1: P-256 ECDSA

**Files:**
- Create: `tools/ecdsa_p256.py`
- Test: `tools/tests/test_ecdsa_p256.py`

- [ ] **Step 1: Write the failing tests.** Create `tools/tests/test_ecdsa_p256.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
        self.assertEqual(ec.der_signature(0x80, 0x7F), bytes.fromhex("300702020080""02017f"))

    def test_spki_matches_upstreams_encoding(self):
        point = ec.parse_spki(UPSTREAM_CUP_SPKI)
        self.assertTrue(ec.on_curve(point))
        self.assertEqual(ec.spki(point), UPSTREAM_CUP_SPKI)

    def test_generated_keys_sign_and_verify(self):
        key = ec.generate_private_key()
        public = ec.public_key(key)
        self.assertTrue(ec.on_curve(public))
        self.assertTrue(ec.verify(public, b"m", *ec.sign(key, b"m")))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run; it fails.**
Run: `cd $WEBOPS && python -m unittest discover -s tools/tests -t tools -p test_ecdsa_p256.py`
Expected: `ModuleNotFoundError: No module named 'ecdsa_p256'`.

- [ ] **Step 3: Implement.** Create `tools/ecdsa_p256.py`:

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""ECDSA over NIST P-256 with SHA-256, in pure Python.

For the updater's test identity only: the test server signs CUP responses
and CRX3 packages with it (Phase 2, sub-project B). Nonces follow RFC 6979,
so signatures are deterministic and testable. It is not constant-time, and
no production key ever goes through it.
"""

from __future__ import annotations

import hashlib
import hmac
import secrets

P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
G = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
     0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)

# DER SubjectPublicKeyInfo of a P-256 key, up to the uncompressed point.
_SPKI_PREFIX = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200")

Point = tuple[int, int]


def _add(p: Point | None, q: Point | None) -> Point | None:
    if p is None:
        return q
    if q is None:
        return p
    (x1, y1), (x2, y2) = p, q
    if x1 == x2 and (y1 + y2) % P == 0:
        return None
    if p == q:
        lam = (3 * x1 * x1 + A) * pow(2 * y1, -1, P) % P
    else:
        lam = (y2 - y1) * pow(x2 - x1, -1, P) % P
    x3 = (lam * lam - x1 - x2) % P
    return x3, (lam * (x1 - x3) - y1) % P


def _mul(k: int, p: Point) -> Point | None:
    result = None
    while k:
        if k & 1:
            result = _add(result, p)
        p = _add(p, p)
        k >>= 1
    return result


def on_curve(p: Point) -> bool:
    x, y = p
    return 0 <= x < P and 0 <= y < P and (y * y - (x * x * x + A * x + B)) % P == 0


def generate_private_key() -> int:
    return secrets.randbelow(N - 1) + 1


def public_key(d: int) -> Point:
    return _mul(d, G)


def _rfc6979_nonce(d: int, digest: bytes) -> int:
    x = d.to_bytes(32, "big")
    h = (int.from_bytes(digest, "big") % N).to_bytes(32, "big")
    v, k = b"\x01" * 32, b"\x00" * 32
    k = hmac.new(k, v + b"\x00" + x + h, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    k = hmac.new(k, v + b"\x01" + x + h, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    while True:
        v = hmac.new(k, v, hashlib.sha256).digest()
        candidate = int.from_bytes(v, "big")
        if 1 <= candidate < N:
            return candidate
        k = hmac.new(k, v + b"\x00", hashlib.sha256).digest()
        v = hmac.new(k, v, hashlib.sha256).digest()


def sign(d: int, message: bytes) -> tuple[int, int]:
    """ECDSA-SHA256 of `message`, as (r, s)."""
    digest = hashlib.sha256(message).digest()
    e = int.from_bytes(digest, "big") % N
    k = _rfc6979_nonce(d, digest)
    r = _mul(k, G)[0] % N
    s = pow(k, -1, N) * (e + r * d) % N
    if not r or not s:
        raise ValueError("degenerate signature; RFC 6979 makes this unreachable")
    return r, s


def verify(q: Point, message: bytes, r: int, s: int) -> bool:
    if not (1 <= r < N and 1 <= s < N):
        return False
    e = int.from_bytes(hashlib.sha256(message).digest(), "big") % N
    w = pow(s, -1, N)
    point = _add(_mul(e * w % N, G), _mul(r * w % N, q))
    return point is not None and point[0] % N == r


def _der_length(n: int) -> bytes:
    return bytes([n]) if n < 0x80 else bytes([0x81, n])


def _der_integer(v: int) -> bytes:
    body = v.to_bytes((v.bit_length() + 8) // 8 or 1, "big")
    return b"\x02" + _der_length(len(body)) + body


def der_signature(r: int, s: int) -> bytes:
    body = _der_integer(r) + _der_integer(s)
    return b"\x30" + _der_length(len(body)) + body


def parse_der_signature(der: bytes) -> tuple[int, int]:
    if der[0] != 0x30 or der[1] != len(der) - 2:
        raise ValueError("not a DER ECDSA signature")
    values, i = [], 2
    for _ in range(2):
        if der[i] != 0x02:
            raise ValueError("not a DER integer")
        length = der[i + 1]
        values.append(int.from_bytes(der[i + 2:i + 2 + length], "big"))
        i += 2 + length
    return values[0], values[1]


def spki(q: Point) -> bytes:
    return _SPKI_PREFIX + b"\x04" + q[0].to_bytes(32, "big") + q[1].to_bytes(32, "big")


def parse_spki(der: bytes) -> Point:
    if not der.startswith(_SPKI_PREFIX + b"\x04") or len(der) != len(_SPKI_PREFIX) + 65:
        raise ValueError("not a P-256 SubjectPublicKeyInfo")
    point = der[len(_SPKI_PREFIX) + 1:]
    return int.from_bytes(point[:32], "big"), int.from_bytes(point[32:], "big")
```

- [ ] **Step 4: Run; they pass.**
Run: `cd $WEBOPS && python -m unittest discover -s tools/tests -t tools -p test_ecdsa_p256.py`
Expected: `Ran 6 tests` … `OK`. If a published RFC 6979 value disagrees, re-read RFC 6979 appendix A.2.5 before touching the code: the test vector is the authority.

- [ ] **Step 5: Mutation check.** Each must make a test fail. Restore after each, as in sub-project A's Task 1 Step 5:
- `% N).to_bytes(32, "big")` → `).to_bytes(32, "big")` in `_rfc6979_nonce`;
- `(v.bit_length() + 8) // 8` → `(v.bit_length() + 7) // 8`;
- `return point is not None and point[0] % N == r` → `return True`.

- [ ] **Step 6: Commit.**

```bash
cd $WEBOPS && git add tools/ecdsa_p256.py tools/tests/test_ecdsa_p256.py && python tools/lint.py \
  && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "tools: P-256 ECDSA for the updater's test keys

The test update server signs CUP responses and CRX3 packages. Python has
no ECDSA in its standard library, so this is a small pure implementation
with RFC 6979 nonces, checked against that RFC's P-256 vectors and
against the encoding of upstream's CUP key. Test identity only."
```

---

### Task 2: CRX3 packages

**Files:**
- Create: `tools/crx3.py`
- Test: `tools/tests/test_crx3.py`

- [ ] **Step 1: Write the failing tests.** Create `tools/tests/test_crx3.py`:

```python
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
```

- [ ] **Step 2: Run; it fails** with `No module named 'crx3'`.

- [ ] **Step 3: Implement.** Create `tools/crx3.py`:

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""CRX3 packages, as components/crx_file/crx3.proto describes them.

    "Cr24" | version 3 (LE32) | header size (LE32) | CrxFileHeader | zip

Each proof signs "CRX3 SignedData\\x00" | LE32 size | signed header data | zip.
Packages built here carry one ECDSA proof, whose key is both the developer
key (it names the CRX) and the publisher key Ghost's updater requires.
"""

from __future__ import annotations

import hashlib
import io
import zipfile
from dataclasses import dataclass

import ecdsa_p256

MAGIC = b"Cr24"
_SIGNATURE_CONTEXT = b"CRX3 SignedData\x00"
_SHA256_WITH_ECDSA = 3
_SIGNED_HEADER_DATA = 10000


def _varint(n: int) -> bytes:
    out = bytearray()
    while True:
        byte, n = n & 0x7F, n >> 7
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _field(number: int, payload: bytes) -> bytes:
    return _varint(number << 3 | 2) + _varint(len(payload)) + payload


def _read_varint(data: bytes, i: int) -> tuple[int, int]:
    value, shift = 0, 0
    while True:
        byte = data[i]
        value |= (byte & 0x7F) << shift
        i, shift = i + 1, shift + 7
        if not byte & 0x80:
            return value, i


def _fields(data: bytes) -> list[tuple[int, bytes]]:
    out, i = [], 0
    while i < len(data):
        tag, i = _read_varint(data, i)
        if tag & 7 != 2:
            raise ValueError("unexpected protobuf wire type")
        length, i = _read_varint(data, i)
        out.append((tag >> 3, data[i:i + length]))
        i += length
    return out


def crx_id(public_key_der: bytes) -> bytes:
    return hashlib.sha256(public_key_der).digest()[:16]


def _zip(files: dict[str, bytes]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, files[name])
    return buffer.getvalue()


def _signed_message(signed_data: bytes, archive: bytes) -> bytes:
    return _SIGNATURE_CONTEXT + len(signed_data).to_bytes(4, "little") + signed_data + archive


def build(files: dict[str, bytes], private_key: int) -> bytes:
    public_der = ecdsa_p256.spki(ecdsa_p256.public_key(private_key))
    signed_data = _field(1, crx_id(public_der))
    archive = _zip(files)
    signature = ecdsa_p256.der_signature(
        *ecdsa_p256.sign(private_key, _signed_message(signed_data, archive)))
    header = (_field(_SHA256_WITH_ECDSA, _field(1, public_der) + _field(2, signature))
              + _field(_SIGNED_HEADER_DATA, signed_data))
    return (MAGIC + (3).to_bytes(4, "little") + len(header).to_bytes(4, "little")
            + header + archive)


@dataclass(frozen=True)
class Package:
    proofs: list[tuple[bytes, bytes]]  # (public key DER, signature DER)
    signed_data: bytes
    crx_id: bytes
    archive: bytes


def parse(data: bytes) -> Package:
    if data[:4] != MAGIC or int.from_bytes(data[4:8], "little") != 3:
        raise ValueError("not a CRX3 package")
    size = int.from_bytes(data[8:12], "little")
    header, archive = data[12:12 + size], data[12 + size:]
    proofs, signed_data = [], b""
    for number, payload in _fields(header):
        if number == _SHA256_WITH_ECDSA:
            proof = dict(_fields(payload))
            proofs.append((proof.get(1, b""), proof.get(2, b"")))
        elif number == _SIGNED_HEADER_DATA:
            signed_data = payload
    return Package(proofs, signed_data, dict(_fields(signed_data)).get(1, b""), archive)


def verified_keys(data: bytes) -> list[bytes]:
    """The public keys whose proofs verify, as the updater would check them."""
    package = parse(data)
    message = _signed_message(package.signed_data, package.archive)
    keys = []
    for public_der, signature in package.proofs:
        try:
            ok = ecdsa_p256.verify(ecdsa_p256.parse_spki(public_der), message,
                                   *ecdsa_p256.parse_der_signature(signature))
        except (ValueError, IndexError):
            ok = False
        if ok:
            keys.append(public_der)
    return keys
```

- [ ] **Step 4: Run; they pass** (`Ran 6 tests` … `OK`).

- [ ] **Step 5: Commit.**

```bash
cd $WEBOPS && git add tools/crx3.py tools/tests/test_crx3.py && python tools/lint.py \
  && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "tools: build and verify CRX3 packages

Omaha 4 delivers a Windows app update as a CRX3 wrapping its installer.
The test server packs mini_installer.exe into one, signed with the test
publisher key, following components/crx_file/crx3.proto."
```

---

### Task 3: The test update server

**Files:**
- Create: `tools/update_server.py`, `test/updater/cup_test_key.json`, `test/updater/crx_test_key.json`, `test/updater/cup_vector.json`, `test/updater/request_all_fields.json`, `test/updater/request_scrubbed.json`, `branding/cup_key.h`, `branding/crx_publisher_key.h`
- Modify: `branding/BUILD.gn`
- Test: `tools/tests/test_update_server.py`

- [ ] **Step 1: Write the golden request files.** These pin the allow-list for both the Python checker and the C++ scrubber (Task 9). Create `test/updater/request_all_fields.json`: one request with every key the 152 serializer can write, plus a key no milestone has added yet (`futurekey`).

```json
{
  "request": {
    "protocol": "4.0", "ismachine": false, "dedup": "cr",
    "acceptformat": "crx3,download,puff,run,xz,zucc",
    "sessionid": "{11111111-1111-4111-8111-111111111111}",
    "requestid": "{22222222-2222-4222-8222-222222222222}",
    "@updater": "ProjectGhostUpdater", "updaterversion": "152.0.7977.149",
    "prodversion": "152.0.7977.149", "updaterchannel": "stable", "prodchannel": "stable",
    "@os": "win", "arch": "x86_64", "wow64": false, "dlpref": "cacheable",
    "domainjoined": false, "extra": "attribute", "futurekey": "x",
    "hw": {"physmemory": 32, "sse": true, "sse2": true, "sse3": true, "sse41": true,
           "sse42": true, "ssse3": true, "avx": true},
    "os": {"platform": "Windows", "arch": "x86_64", "version": "10.0.26120.1", "sp": "",
           "futurekey": "x"},
    "updaters": {"name": "ProjectGhostUpdater", "ismachine": false,
                 "autoupdatecheckenabled": true, "updatepolicy": 1,
                 "version": "152.0.7977.149", "lastchecked": 0, "laststarted": 0},
    "apps": [{
      "appid": "{33333333-3333-4333-8333-333333333333}", "version": "152.0.7977.14901",
      "ap": "", "brand": "GGLS", "lang": "en-US", "installdate": 7000, "iid": "{44444444-4444-4444-8444-444444444444}",
      "installsource": "taggedmi", "installedby": "other", "release_channel": "stable",
      "cohort": "1:a:", "cohortname": "Stable", "cohorthint": "x", "enabled": true,
      "_installer_attribute": "x", "futurekey": "x",
      "cached_items": [{"sha256": "00", "futurekey": "x"}],
      "disabled": [{"reason": 0, "futurekey": "x"}],
      "updatecheck": {"updatedisabled": true, "rollback_allowed": true,
                      "sameversionupdate": true, "targetversionprefix": "152.",
                      "futurekey": "x"},
      "data": [{"name": "install", "index": "verboselogging", "#text": "untrusted",
                "futurekey": "x"}],
      "ping": {"ping_freshness": "{x}", "ad": 7000, "a": 1, "rd": 7000, "r": 1},
      "events": [{"eventtype": 2, "eventresult": 1}]
    }]
  }
}
```

Create `test/updater/request_scrubbed.json`: the same request after the allow-list.

```json
{
  "request": {
    "protocol": "4.0", "ismachine": false,
    "acceptformat": "crx3,download,puff,run,xz,zucc",
    "sessionid": "{11111111-1111-4111-8111-111111111111}",
    "requestid": "{22222222-2222-4222-8222-222222222222}",
    "@updater": "ProjectGhostUpdater", "updaterversion": "152.0.7977.149",
    "prodversion": "152.0.7977.149", "updaterchannel": "stable", "prodchannel": "stable",
    "@os": "win", "arch": "x86_64", "wow64": false, "dlpref": "cacheable",
    "os": {"platform": "Windows", "arch": "x86_64", "version": "10.0.26120.1"},
    "apps": [{
      "appid": "{33333333-3333-4333-8333-333333333333}", "version": "152.0.7977.14901",
      "ap": "", "brand": "GGLS", "release_channel": "stable", "enabled": true,
      "cached_items": [{"sha256": "00"}],
      "disabled": [{"reason": 0}],
      "updatecheck": {"updatedisabled": true, "rollback_allowed": true,
                      "sameversionupdate": true, "targetversionprefix": "152."},
      "data": [{"name": "install", "index": "verboselogging"}]
    }]
  }
}
```

- [ ] **Step 2: Write the failing tests.** Create `tools/tests/test_update_server.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import datetime
import hashlib
import json
import tempfile
import threading
import unittest
import urllib.request
from pathlib import Path

import crx3
import ecdsa_p256 as ec
import repo
import update_server as us

DATA = repo.REPO_ROOT / "test" / "updater"
APPID = "{33333333-3333-4333-8333-333333333333}"


def load(name: str) -> dict:
    return json.loads((DATA / name).read_text(encoding="utf-8"))


class AllowListTest(unittest.TestCase):
    def test_scrubbed_golden_is_allowed(self):
        self.assertEqual(us.disallowed_keys(load("request_scrubbed.json")), [])

    def test_every_dropped_key_is_reported(self):
        flagged = set(us.disallowed_keys(load("request_all_fields.json")))
        for path in ("request.dedup", "request.hw", "request.domainjoined", "request.updaters",
                     "request.futurekey", "request.os.sp", "request.apps[0].iid",
                     "request.apps[0].installdate", "request.apps[0].lang",
                     "request.apps[0].ping", "request.apps[0].events",
                     "request.apps[0].cohort", "request.apps[0].data[0].#text",
                     "request.apps[0].updatecheck.futurekey"):
            self.assertIn(path, flagged)

    def test_scrub_reference_matches_the_golden_output(self):
        # The C++ scrubber is tested against the same pair (Task 9).
        self.assertEqual(us.scrub(load("request_all_fields.json")), load("request_scrubbed.json"))


class CupTest(unittest.TestCase):
    def test_proof_verifies_like_cup_cc(self):
        key = us.load_key(us.CUP_KEY_FILE)
        proof = us.cup_proof(key, "1:42", b"request", b"response")
        self.assertTrue(us.cup_verify(ec.public_key(key), "1:42", b"request", b"response", proof))
        self.assertFalse(us.cup_verify(ec.public_key(key), "1:43", b"request", b"response", proof))
        self.assertEqual(proof.split(":")[1], hashlib.sha256(b"request").hexdigest())

    def test_vector_is_current(self):
        vector = load("cup_vector.json")
        key = us.load_key(us.CUP_KEY_FILE)
        self.assertEqual(us.cup_proof(key, vector["cup2key"], vector["request"].encode(),
                                      vector["response"].encode()), vector["proof"])


class KeyHeaderTest(unittest.TestCase):
    def test_headers_match_the_test_keys(self):
        cup = us.load_key(us.CUP_KEY_FILE)
        crx = us.load_key(us.CRX_KEY_FILE)
        self.assertEqual(us.CUP_HEADER.read_text(encoding="utf-8"),
                         us.render_cup_header(us.CUP_KEY_VERSION, ec.spki(ec.public_key(cup))))
        self.assertEqual(us.CRX_HEADER.read_text(encoding="utf-8"),
                         us.render_crx_header(ec.spki(ec.public_key(crx))))


class ResponseTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.crx = Path(tmp.name) / "update.crx3"
        self.crx.write_bytes(crx3.build({"mini_installer.exe": b"MZ"}, 1234))
        self.offer = us.Offer(APPID, "152.0.7977.14902", self.crx, "mini_installer.exe",
                              "--verbose-logging --do-not-launch-chrome")

    def check(self, version: str) -> dict:
        request = {"request": {"apps": [{"appid": APPID, "version": version, "updatecheck": {}}]}}
        response = us.respond(request, self.offer, "http://127.0.0.1:8484",
                              datetime.date(2026, 10, 2))
        self.assertEqual(response["response"]["protocol"], "4.0")
        self.assertEqual(response["response"]["daystart"]["elapsed_days"], 7214)
        return response["response"]["apps"][0]

    def test_older_version_gets_the_crx(self):
        app = self.check("152.0.7977.14901")
        self.assertEqual(app["updatecheck"]["nextversion"], "152.0.7977.14902")
        download, install = app["updatecheck"]["pipelines"][0]["operations"]
        digest = hashlib.sha256(self.crx.read_bytes()).hexdigest()
        self.assertEqual(download["type"], "download")
        self.assertEqual(download["out"]["sha256"], digest)
        self.assertEqual(download["size"], self.crx.stat().st_size)
        self.assertEqual(download["urls"][0]["url"], "http://127.0.0.1:8484/download/update.crx3")
        self.assertEqual(install, {"type": "crx3", "in": {"sha256": digest},
                                   "path": "mini_installer.exe",
                                   "arguments": "--verbose-logging --do-not-launch-chrome"})

    def test_current_version_gets_noupdate(self):
        self.assertEqual(self.check("152.0.7977.14902")["updatecheck"], {"status": "noupdate"})

    def test_other_apps_get_noupdate(self):
        request = {"request": {"apps": [{"appid": "{other}", "version": "1.0.0.0",
                                         "updatecheck": {}}]}}
        app = us.respond(request, self.offer, "http://x", datetime.date(2026, 10, 2))
        self.assertEqual(app["response"]["apps"][0]["updatecheck"], {"status": "noupdate"})


class ServerTest(unittest.TestCase):
    def test_round_trip_with_cup_and_download(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        crx = Path(tmp.name) / "update.crx3"
        crx.write_bytes(crx3.build({"mini_installer.exe": b"MZ"}, 1234))
        log = Path(tmp.name) / "requests.jsonl"
        key = us.load_key(us.CUP_KEY_FILE)
        server = us.UpdateServer(("127.0.0.1", 0), us.Offer(
            APPID, "152.0.7977.14902", crx, "mini_installer.exe", ""), key, log)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.shutdown)
        base = f"http://127.0.0.1:{server.server_address[1]}"

        body = json.dumps({"request": {"apps": [{"appid": APPID, "version": "1.0.0.0",
                                                  "updatecheck": {}}]}}).encode()
        request = urllib.request.Request(base + "/update?cup2key=1:7&cup2hreq=x", data=body)
        with urllib.request.urlopen(request) as response:
            payload = response.read()
            proof = response.headers["X-Cup-Server-Proof"]
        self.assertTrue(payload.startswith(us.RESPONSE_PREFIX.encode()))
        self.assertTrue(us.cup_verify(ec.public_key(key), "1:7", body, payload, proof))
        self.assertEqual(json.loads(log.read_text(encoding="utf-8").splitlines()[0])["body"],
                         json.loads(body))

        ranged = urllib.request.Request(base + "/download/update.crx3",
                                        headers={"Range": "bytes=0-3"})
        with urllib.request.urlopen(ranged) as response:
            self.assertEqual(response.status, 206)
            self.assertEqual(response.read(), b"Cr24")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run; it fails** with `No module named 'update_server'`.

- [ ] **Step 4: Implement.** Create `tools/update_server.py`:

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Omaha 4 test server for Ghost's updater under the test identity.

  keygen [--force]   new test CUP and CRX publisher keys, and their headers
                     in branding/ (sub-project D replaces both)
  vector             rewrite test/updater/cup_vector.json from the CUP key
  crx --installer I --out O
                     pack an installer into a CRX3 signed with the test
                     publisher key
  serve --crx C --version V --appid A [--port 8484] [--log L]
                     answer update checks, sign them with CUP, serve C

The server answers with an update when the request's version of the app is
older than V. It records every request body, so the end-to-end test can
check them against the allow-list. Loopback only; test keys only.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import http.server
import json
import sys
import urllib.parse
from dataclasses import dataclass
from pathlib import Path

import crx3
import ecdsa_p256
import repo

RESPONSE_PREFIX = ")]}'\n"
CUP_KEY_VERSION = 1
TEST_DIR = repo.REPO_ROOT / "test" / "updater"
CUP_KEY_FILE = TEST_DIR / "cup_test_key.json"
CRX_KEY_FILE = TEST_DIR / "crx_test_key.json"
CUP_VECTOR_FILE = TEST_DIR / "cup_vector.json"
CUP_HEADER = repo.REPO_ROOT / "branding" / "cup_key.h"
CRX_HEADER = repo.REPO_ROOT / "branding" / "crx_publisher_key.h"
_DAY_ZERO = datetime.date(2007, 1, 1)

# The keys Ghost's update requests may carry (spec: the request scrubber).
# None is a value; a dict is an object; a one-element list is a list of
# objects. components/update_client/request_scrubber.cc keeps the same keys;
# both are tested against test/updater/request_*.json.
_APP = {
    "appid": None, "version": None, "ap": None, "brand": None, "release_channel": None,
    "enabled": None, "disabled": [{"reason": None}], "cached_items": [{"sha256": None}],
    "updatecheck": {"updatedisabled": None, "rollback_allowed": None,
                    "sameversionupdate": None, "targetversionprefix": None},
    "data": [{"name": None, "index": None}],
}
ALLOWED = {"request": {
    "protocol": None, "ismachine": None, "acceptformat": None, "sessionid": None,
    "requestid": None, "@updater": None, "updaterversion": None, "prodversion": None,
    "updaterchannel": None, "prodchannel": None, "@os": None, "arch": None, "wow64": None,
    "dlpref": None, "os": {"platform": None, "arch": None, "version": None}, "apps": [_APP],
}}


def disallowed_keys(value, schema=ALLOWED, path: str = "") -> list[str]:
    if schema is None:
        return [f"{path} (not a value)"] if isinstance(value, (dict, list)) else []
    if isinstance(schema, list):
        if not isinstance(value, list):
            return [f"{path} (not a list)"]
        return [p for i, item in enumerate(value)
                for p in disallowed_keys(item, schema[0], f"{path}[{i}]")]
    if not isinstance(value, dict):
        return [f"{path} (not an object)"]
    out = []
    for key, item in value.items():
        child = f"{path}.{key}" if path else key
        out += [child] if key not in schema else disallowed_keys(item, schema[key], child)
    return out


def scrub(value, schema=ALLOWED):
    """The reference scrubber: what request_scrubber.cc must produce."""
    if schema is None:
        return value
    if isinstance(schema, list):
        return [scrub(item, schema[0]) for item in value]
    return {key: scrub(item, schema[key]) for key, item in value.items() if key in schema}


# --- Keys -------------------------------------------------------------------------------

def load_key(path: Path) -> int:
    return int(json.loads(path.read_text(encoding="utf-8"))["private_key"], 16)


def _write_key(path: Path, key: int, purpose: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({
        "comment": f"Test identity only: {purpose}. Never used for a release; "
                   "sub-project D creates the production keys.",
        "private_key": f"{key:064x}"}, indent=2) + "\n", encoding="utf-8", newline="\n")


def _bytes_list(data: bytes, indent: str = "    ") -> str:
    lines = [", ".join(f"0x{b:02x}" for b in data[i:i + 12]) for i in range(0, len(data), 12)]
    return ",\n".join(indent + line for line in lines)


_HEADER_TOP = """// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Generated by tools/update_server.py keygen. Test identity only: the
// private key is in {key_file}. Sub-project D replaces both.
"""


def render_cup_header(version: int, public_der: bytes) -> str:
    return (_HEADER_TOP.format(key_file="test/updater/cup_test_key.json")
            + "\n#ifndef GHOST_BRANDING_CUP_KEY_H_\n#define GHOST_BRANDING_CUP_KEY_H_\n\n"
            "#include <stdint.h>\n\n#include <array>\n\nnamespace ghost {\n\n"
            "// The CUP key version the client announces in cup2key.\n"
            f"inline constexpr int kCupKeyVersion = {version};\n\n"
            "// DER SubjectPublicKeyInfo of the ECDSA P-256 key that signs update\n"
            "// responses (patches/0020).\n"
            "inline constexpr auto kCupPublicKey = std::to_array<uint8_t>({\n"
            f"{_bytes_list(public_der)},\n}});\n\n"
            "}  // namespace ghost\n\n#endif  // GHOST_BRANDING_CUP_KEY_H_\n")


def render_crx_header(public_der: bytes) -> str:
    digest = hashlib.sha256(public_der).digest()
    return (_HEADER_TOP.format(key_file="test/updater/crx_test_key.json")
            + "\n#ifndef GHOST_BRANDING_CRX_PUBLISHER_KEY_H_\n"
            "#define GHOST_BRANDING_CRX_PUBLISHER_KEY_H_\n\n"
            "#include <stdint.h>\n\n#include <array>\n\nnamespace ghost {\n\n"
            "// SHA-256 of the DER SubjectPublicKeyInfo of the key whose proof\n"
            "// CRX3_WITH_GHOST_PUBLISHER_PROOF requires (patches/0018).\n"
            "inline constexpr std::array<uint8_t, 32> kCrxPublisherKeyHash = {\n"
            f"{_bytes_list(digest)},\n}};\n\n"
            "}  // namespace ghost\n\n#endif  // GHOST_BRANDING_CRX_PUBLISHER_KEY_H_\n")


def write_headers() -> None:
    cup_der = ecdsa_p256.spki(ecdsa_p256.public_key(load_key(CUP_KEY_FILE)))
    crx_der = ecdsa_p256.spki(ecdsa_p256.public_key(load_key(CRX_KEY_FILE)))
    CUP_HEADER.write_text(render_cup_header(CUP_KEY_VERSION, cup_der), encoding="utf-8",
                          newline="\n")
    CRX_HEADER.write_text(render_crx_header(crx_der), encoding="utf-8", newline="\n")


# --- CUP -------------------------------------------------------------------------------

def cup_proof(key: int, cup2key: str, request_body: bytes, response_body: bytes) -> str:
    """X-Cup-Server-Proof for a response, as client_update_protocol/cup.cc checks it."""
    request_hash = hashlib.sha256(request_body).digest()
    inner = hashlib.sha256(request_hash + hashlib.sha256(response_body).digest()
                           + cup2key.encode()).digest()
    signature = ecdsa_p256.der_signature(*ecdsa_p256.sign(key, inner))
    return f"{signature.hex()}:{request_hash.hex()}"


def cup_verify(public: tuple[int, int], cup2key: str, request_body: bytes,
               response_body: bytes, proof: str) -> bool:
    signature_hex, _, hash_hex = proof.partition(":")
    request_hash = hashlib.sha256(request_body).digest()
    if bytes.fromhex(hash_hex) != request_hash:
        return False
    inner = hashlib.sha256(request_hash + hashlib.sha256(response_body).digest()
                           + cup2key.encode()).digest()
    return ecdsa_p256.verify(public, inner,
                             *ecdsa_p256.parse_der_signature(bytes.fromhex(signature_hex)))


def write_vector() -> None:
    request = '{"request":{"protocol":"4.0"}}'
    response = RESPONSE_PREFIX + '{"response":{"protocol":"4.0"}}'
    cup2key = f"{CUP_KEY_VERSION}:12345"
    CUP_VECTOR_FILE.write_text(json.dumps({
        "comment": "Generated by tools/update_server.py vector. Checked by "
                   "ghost/updater/cup_unittest.cc with Chromium's CUP verifier.",
        "nonce": 12345, "cup2key": cup2key, "request": request, "response": response,
        "proof": cup_proof(load_key(CUP_KEY_FILE), cup2key, request.encode(),
                           response.encode())}, indent=2) + "\n",
        encoding="utf-8", newline="\n")


# --- Responses ------------------------------------------------------------------------

@dataclass(frozen=True)
class Offer:
    appid: str
    version: str
    crx: Path
    installer: str
    arguments: str


def _version_tuple(text: str) -> tuple[int, ...]:
    try:
        return tuple(int(p) for p in text.split("."))
    except ValueError:
        return ()


def respond(request: dict, offer: Offer | None, base_url: str, today: datetime.date) -> dict:
    apps = []
    for app in request.get("request", {}).get("apps", []):
        appid = app.get("appid", "")
        entry = {"appid": appid, "status": "ok"}
        if "updatecheck" in app:
            if (offer and appid.lower() == offer.appid.lower()
                    and _version_tuple(app.get("version", "")) < _version_tuple(offer.version)):
                data = offer.crx.read_bytes()
                digest = hashlib.sha256(data).hexdigest()
                entry["updatecheck"] = {
                    "status": "ok", "nextversion": offer.version,
                    "pipelines": [{"pipeline_id": "full", "operations": [
                        {"type": "download", "size": len(data), "out": {"sha256": digest},
                         "urls": [{"url": f"{base_url}/download/{offer.crx.name}"}]},
                        {"type": "crx3", "in": {"sha256": digest}, "path": offer.installer,
                         "arguments": offer.arguments}]}]}
            else:
                entry["updatecheck"] = {"status": "noupdate"}
        apps.append(entry)
    return {"response": {"protocol": "4.0", "server": "ghost-test",
                         "daystart": {"elapsed_days": (today - _DAY_ZERO).days},
                         "apps": apps}}


# --- HTTP -----------------------------------------------------------------------------

class _Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"  # BITS downloads need HTTP/1.1 and ranges
    server: UpdateServer

    def do_POST(self) -> None:
        url = urllib.parse.urlsplit(self.path)
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        query = urllib.parse.parse_qs(url.query)
        self.server.record(self.path, body)
        if url.path != "/update":
            self._send(404, b"")
            return
        try:
            request = json.loads(body)
        except ValueError:
            self._send(400, b"")
            return
        payload = (RESPONSE_PREFIX + json.dumps(respond(
            request, self.server.offer, self.server.base_url, datetime.date.today()))).encode()
        headers = {"Content-Type": "application/json"}
        if "cup2key" in query:
            headers["X-Cup-Server-Proof"] = cup_proof(self.server.cup_key, query["cup2key"][0],
                                                      body, payload)
        self._send(200, payload, headers)

    def _download(self, head: bool) -> None:
        offer = self.server.offer
        if not offer or self.path != f"/download/{offer.crx.name}":
            self._send(404, b"", head=head)
            return
        data = offer.crx.read_bytes()
        headers = {"Content-Type": "application/octet-stream", "Accept-Ranges": "bytes"}
        spec = self.headers.get("Range", "")
        if spec.startswith("bytes="):
            first, _, last = spec[6:].partition("-")
            start = int(first or 0)
            end = min(int(last), len(data) - 1) if last else len(data) - 1
            headers["Content-Range"] = f"bytes {start}-{end}/{len(data)}"
            self._send(206, data[start:end + 1], headers, head)
        else:
            self._send(200, data, headers, head)

    def do_GET(self) -> None:
        self._download(head=False)

    def do_HEAD(self) -> None:
        self._download(head=True)

    def _send(self, status: int, body: bytes, headers: dict | None = None,
              head: bool = False) -> None:
        self.send_response(status)
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if not head:
            self.wfile.write(body)

    def log_message(self, format: str, *args) -> None:
        pass  # request bodies go to the JSONL log instead


class UpdateServer(http.server.ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], offer: Offer | None, cup_key: int,
                 log: Path | None):
        super().__init__(address, _Handler)
        self.offer, self.cup_key, self.log = offer, cup_key, log
        self.base_url = f"http://{address[0]}:{self.server_address[1]}"

    def record(self, path: str, body: bytes) -> None:
        if not self.log:
            return
        try:
            parsed = json.loads(body) if body else None
        except ValueError:
            parsed = {"unparsed": body.decode("utf-8", "replace")}
        with self.log.open("a", encoding="utf-8") as f:
            f.write(json.dumps({"path": path, "body": parsed}) + "\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    keygen = sub.add_parser("keygen")
    keygen.add_argument("--force", action="store_true")
    sub.add_parser("vector")
    crx = sub.add_parser("crx")
    crx.add_argument("--installer", type=Path, required=True)
    crx.add_argument("--out", type=Path, required=True)
    crx.add_argument("--key", type=Path, default=CRX_KEY_FILE)
    serve = sub.add_parser("serve")
    serve.add_argument("--crx", type=Path, required=True)
    serve.add_argument("--version", required=True)
    serve.add_argument("--appid", required=True)
    serve.add_argument("--installer", default="mini_installer.exe")
    serve.add_argument("--arguments", default="--verbose-logging --do-not-launch-chrome")
    serve.add_argument("--port", type=int, default=8484)
    serve.add_argument("--log", type=Path)
    args = parser.parse_args(argv)

    if args.command == "keygen":
        if (CUP_KEY_FILE.exists() or CRX_KEY_FILE.exists()) and not args.force:
            print("test keys exist; pass --force to replace them", file=sys.stderr)
            return 1
        _write_key(CUP_KEY_FILE, ecdsa_p256.generate_private_key(), "signs CUP responses")
        _write_key(CRX_KEY_FILE, ecdsa_p256.generate_private_key(),
                   "signs CRX3 packages (developer and publisher proof)")
        write_headers()
        write_vector()
    elif args.command == "vector":
        write_vector()
    elif args.command == "crx":
        args.out.write_bytes(crx3.build({args.installer.name: args.installer.read_bytes()},
                                        load_key(args.key)))
    else:
        server = UpdateServer(("127.0.0.1", args.port),
                              Offer(args.appid, args.version, args.crx, args.installer,
                                    args.arguments),
                              load_key(CUP_KEY_FILE), args.log)
        print(f"serving {args.crx.name} as {args.version} on {server.base_url}", flush=True)
        server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5: Generate the keys, headers and vector.**

```bash
cd $WEBOPS/tools && python update_server.py keygen && cd .. && ls test/updater branding
```

Expected: `cup_test_key.json`, `crx_test_key.json`, `cup_vector.json`, `request_*.json`; `branding/cup_key.h`, `branding/crx_publisher_key.h`.

- [ ] **Step 6: The headers' target.** In `branding/BUILD.gn`, add:

```gn
# The updater's keys, compiled into update_client and crx_file
# (patches/0018, 0020). Test identity until sub-project D.
source_set("keys") {
  sources = [
    "crx_publisher_key.h",
    "cup_key.h",
  ]
}
```

- [ ] **Step 7: Run; the tests pass.**
Run: `cd $WEBOPS && python -m unittest discover -s tools/tests -t tools -p test_update_server.py`
Expected: `Ran 10 tests` … `OK`.

- [ ] **Step 8: Mutation check.** Each must fail a test. Restore after each:
- in `cup_proof`, drop `+ cup2key.encode()`;
- in `disallowed_keys`, `out += [child] if key not in schema` → `out += []`;
- in `respond`, `<` → `<=`.

- [ ] **Step 9: Commit.**

```bash
cd $WEBOPS && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "tools: the updater's test server, test keys and allow-list

update_server.py answers Omaha 4 update checks with a CRX3 update, signs
each response with CUP, serves the package with ranges for BITS, and
records every request body. keygen made the test identity's CUP and CRX
publisher keys and their headers in branding/. The allow-list and the
golden request pair in test/updater/ pin what an update request may hold."
```

---

### Task 4: The offline installer tool

**Files:**
- Create: `tools/offline_installer.py`
- Test: `tools/tests/test_offline_installer.py`

- [ ] **Step 1: Write the failing tests.** Create `tools/tests/test_offline_installer.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

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

    def test_install_arguments(self):
        self.assertEqual(oi.install_arguments("{APPID}"),
                         ["--install=appguid={APPID}&appname=Project%20Ghost&needsadmin=False",
                          "--silent"])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run; it fails** with `No module named 'offline_installer'`.

- [ ] **Step 3: Implement.** Create `tools/offline_installer.py`:

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Builds Ghost's offline installer: UpdaterSetup.exe with the browser inside.

Upstream's chrome/updater/win/signing/sign.py packs the browser installer
and an offline manifest into the metainstaller. It needs pywin32, which
Chromium's vpython3 environment (src/.vpython3) provides. Until sub-project
D signs and tags installers, the result is unsigned and untagged, so the
install arguments go on its command line (install_arguments()).

  python tools/offline_installer.py --src C:\\...\\src --out out\\vanilla
      --version 152.0.7977.14901 --appid {...} --output ProjectGhostOfflineSetup.exe
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

# protocol 3.0 XML, as upstream's offline example; sign.py fills in ${...}.
MANIFEST = """<?xml version="1.0" encoding="UTF-8"?>
<response protocol="3.0">
  <systemrequirements platform="win" arch="${ARCH_REQUIREMENT}" min_os_version="10.0"/>
  <app appid="${APP_ID}" status="ok">
    <updatecheck status="ok">
      <urls>
        <url codebase="http://127.0.0.1:8484/download/"/>
      </urls>
      <manifest version="${INSTALLER_VERSION}">
        <packages>
          <package name="${INSTALLER_FILENAME}" hash_sha256="${INSTALLER_HASH_SHA256}" size="${INSTALLER_SIZE}" required="true"/>
        </packages>
        <actions>
          <action event="install" run="${INSTALLER_FILENAME}" arguments="--verbose-logging --do-not-launch-chrome" needsadmin="false"/>
          <action event="postinstall" onsuccess="exitsilentlyonlaunchcmd"/>
        </actions>
      </manifest>
    </updatecheck>
  </app>
</response>
"""


def sign_command(depot_tools: Path, out_dir: Path, installer: Path, manifest: Path, appid: str,
                 version: str, output: Path) -> list[str]:
    signing = out_dir / "UpdaterSigning"
    replacements = {"${INSTALLER_VERSION}": version, "${ARCH_REQUIREMENT}": "x64"}
    return [str(depot_tools / "vpython3.bat"), str(signing / "sign.py"),
            "--in_file", str(out_dir / "UpdaterSetup.exe"), "--out_file", str(output),
            "--appid", appid, "--installer_path", str(installer),
            "--manifest_path", str(manifest), "--lzma_7z", str(signing / "7zr.exe"),
            "--disable_tag_and_sign", "--manifest_dict_replacements", repr(replacements)]


def install_arguments(appid: str) -> list[str]:
    return [f"--install=appguid={appid}&appname=Project%20Ghost&needsadmin=False", "--silent"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--src", type=Path, required=True, help="Chromium checkout (src)")
    parser.add_argument("--out", type=Path, required=True, help="output dir, relative to --src")
    parser.add_argument("--version", required=True, help="the browser installer's version")
    parser.add_argument("--appid", required=True, help="the browser's app ID")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--depot-tools", type=Path, default=Path(r"C:\src\depot_tools"))
    args = parser.parse_args(argv)
    out_dir = (args.src / args.out).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        manifest = Path(tmp) / "OfflineManifest.gup"
        manifest.write_text(MANIFEST, encoding="utf-8", newline="\n")
        command = sign_command(args.depot_tools, out_dir, out_dir / "mini_installer.exe",
                               manifest, args.appid, args.version, args.output.resolve())
        return subprocess.run(command, cwd=out_dir / "UpdaterSigning").returncode


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run; they pass** (`Ran 3 tests` … `OK`).

- [ ] **Step 5: Commit.**

```bash
cd $WEBOPS && git add tools/offline_installer.py tools/tests/test_offline_installer.py \
  && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "tools: build the offline installer through upstream sign.py

Packs mini_installer.exe and an offline manifest into UpdaterSetup.exe,
unsigned and untagged until sub-project D, using vpython3 for sign.py's
pywin32 dependency."
```

---

### Task 5: The browser moves under the company directory

**Files:**
- Modify: `branding/install_modes.h` (`kCompanyPathName`, `kProductPathName`, comment)
- Modify: `tools/installer_smoke.py`, `tools/tests/test_installer_smoke.py`

- [ ] **Step 1: Update the smoke test's tests first.** In `tools/tests/test_installer_smoke.py`:
- `EXP` gains `company_path="Project Ghost"`, and its `product_path` becomes `"Browser"`.
- `CHROME` becomes `LOCALAPPDATA + r"\ProjectGhost\Browser\Application\chrome.exe"`.
- `ExpectationsTest` expects `product_path="Browser", company_path="Project Ghost"`.

Add:

```python
class LayoutTest(unittest.TestCase):
    def test_company_and_product_paths(self):
        self.assertEqual(EXP.install_dir_parts, ("Project Ghost", "Browser"))
        self.assertEqual(EXP.registry_root, "Project Ghost")
        self.assertEqual(EXP.uninstall_key, "Project Ghost Browser")

    def test_without_a_company(self):
        exp = installer_smoke.Expectations(**{**EXP.__dict__, "company_path": ""})
        self.assertEqual(exp.install_dir_parts, ("Browser",))
        self.assertEqual(exp.registry_root, "Browser")
        self.assertEqual(exp.uninstall_key, "Browser")
```

Run `python -m unittest discover -s tools/tests -t tools -p test_installer_smoke.py`. Expected: errors on `company_path`.

- [ ] **Step 2: Implement in `tools/installer_smoke.py`.**
- `Expectations` gains, after `product_path`:

  ```python
      company_path: str     # kCompanyPathName: the directory above product_path, may be empty
  ```

  and these properties:

  ```python
      @property
      def install_dir_parts(self) -> tuple[str, ...]:
          return tuple(p for p in (self.company_path, self.product_path) if p)

      @property
      def registry_root(self) -> str:
          return self.company_path or self.product_path

      @property
      def uninstall_key(self) -> str:
          return " ".join(self.install_dir_parts)
  ```

- In `expectations()`, delete the `if field(r'kCompanyPathName…')` check and its `raise`. Pass `company_path=field(r'kCompanyPathName\[\]\s*=\s*L"([^"]*)"'),`.
- `_registration_kinds`: the `software` row becomes `(f"Software\\{exp.registry_root}", "software", lambda k: k == exp.registry_root)`.
- `snapshot()`: `app_dir = Path(os.environ["LOCALAPPDATA"]).joinpath(*exp.install_dir_parts) / "Application"`. The uninstall key path ends in `rf"\{exp.uninstall_key}"` instead of `rf"\{exp.product_path}"`.
- The module docstring's first bullet names "the install directory under the company directory".

Check that nothing else uses `product_path` as a path: `grep -n "product_path" tools/installer_smoke.py`. Only the field, `install_dir_parts` and `registry_root` should remain.

- [ ] **Step 3: Run all tooling tests; they pass.**

- [ ] **Step 4: Change the identity.** In `branding/install_modes.h`:

```cpp
// The company directory holds the browser and, beside it, the updater
// (branding/updater.gni uses the same company name). Uninstalling the browser
// clears Software\<company>\<product>, so the updater's
// Software\<company>\Update survives it.
inline constexpr wchar_t kCompanyPathName[] = L"Project Ghost";

inline constexpr wchar_t kProductPathName[] = L"Browser";
```

Update the file's header comment: "the user data and registry path (`kCompanyPathName\kProductPathName`)".

- [ ] **Step 5: Commit, build, smoke-test in Sandbox.** Phase 1's install, launch and uninstall must still pass in the new layout.

```bash
cd $WEBOPS && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "branding: put the browser under a Project Ghost company directory

Uninstall clears the browser's registry key, so the updater's key cannot
sit inside it. As Google and Brave do, the browser becomes Project
Ghost\\Browser and the updater will sit beside it. The smoke test learns
the company level."
git -C $SRC/ghost pull --ff-only
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 chrome mini_installer" 2>&1 | tail -1
cd $WEBOPS && python tools/installer_smoke.py sandbox --installer chromium/src/out/vanilla/mini_installer.exe
```

Expected: `PASSED`. If `uninstall` fails because `Software\Project Ghost` survives empty, read `chrome_installer.log` in the results directory. Two options:
- if upstream leaves an empty company key by design (Chrome leaves `Software\Google`), change the uninstalled check to look for `Software\ProjectGhost\Browser`;
- otherwise, find what is left in it.

Record the outcome in the spike notes.

---

### Task 6: The updater identity, and building the updater

**Files:**
- Create: `branding/updater.gni` (generated once by the script below, then committed)
- Modify: `build/args/dev.gn`, `branding/install_modes.h` (`app_guid`)
- Modify (patch 0015): `$SRC/chrome/updater/branding.gni`

- [ ] **Step 1: Generate `branding/updater.gni`.** Run this once; it reads upstream's Chromium branch and gives every GUID a fresh value:

```bash
cd $WEBOPS && python - <<'EOF'
import re, uuid
from pathlib import Path
src = Path("chromium/src/chrome/updater/branding.gni").read_text(encoding="utf-8")
body = src[src.index("} else {\n") + len("} else {\n"):src.rindex("}")]
values = {
    "browser_name": "Project Ghost", "browser_product_name": "Project Ghost",
    "crash_product_name": "ProjectGhostUpdater", "crash_upload_url": "",
    "help_center_url": "", "app_logo_url": "",
    "keystone_app_name": "ProjectGhostSoftwareUpdate",
    "keystone_bundle_identifier": "org.projectghost.Keystone",
    "mac_browser_bundle_identifier": "org.projectghost.Browser",
    "mac_updater_bundle_identifier": "org.projectghost.ProjectGhostUpdater",
    "privileged_helper_bundle_name": "ProjectGhostUpdaterPrivilegedHelper",
    "privileged_helper_name": "org.projectghost.Browser.UpdaterPrivilegedHelper",
    "updater_company_full_name": "Project Ghost",
    "updater_company_short_name": "Project Ghost",
    "updater_company_short_name_lowercase": "projectghost",
    "updater_company_short_name_uppercase": "PROJECTGHOST",
    "updater_copyright": "Copyright 2026 The Project Ghost Authors.",
    "updater_product_full_name": "ProjectGhostUpdater",
    "updater_product_full_name_dashed_lowercase": "projectghost-updater",
    "updater_product_full_display_name": "Project Ghost Updater",
    "updater_metainstaller_name": "Project Ghost Installer",
    "legacy_service_name_prefix": "pgupdate",
    "update_check_url": "http://127.0.0.1:8484/update",
    "updater_event_logging_url": "",
}
def assignment(m):
    name, value = m.group(1), m.group(2)
    if name in values:
        return f'{name} = "{values[name]}"'
    g = re.fullmatch(r"(\{)?([0-9A-Fa-f-]{36})(\})?", value)
    if g:
        fresh = str(uuid.uuid4())
        if not any(c in "abcdef" for c in g.group(2)):
            fresh = fresh.upper()  # keep the upstream value's case
        return f'{name} = "{g.group(1) or ""}{fresh}{g.group(3) or ""}"'
    return m.group(0)
body = re.sub(r'(\w+) =\s*\n?\s*"([^"]*)"', assignment, body)
header = """# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Ghost's updater identity, imported by chrome/updater/branding.gni
# (patches/0015) in place of its Chromium values. Generated from those values
# on 2026-10-02; every GUID is new.
#
# TEST IDENTITY (Phase 2): the names, GUIDs and the loopback update URL are
# replaced when the final product name is chosen. updater_company_short_name
# must equal kCompanyPathName in branding/install_modes.h.

"""
Path("branding/updater.gni").write_text(header + body.replace("\n  ", "\n").strip() + "\n",
                                        encoding="utf-8", newline="\n")
EOF
grep -c "=" branding/updater.gni; grep -n "Chromium\|google\|Google" branding/updater.gni
```

Expected:
- about 80 assignments;
- the second grep finds only `grdfile_name = "chromium_strings"`, `extra_args_is_chrome_branded` and `updater_app_icon_path` (the macOS icon). They're kept on purpose:
  - the updater's UI strings come from Chromium's string file, and the audit (Task 13) checks what they show;
  - the other two are build plumbing.

Read the file through. Every GUID must differ from the upstream file:

```bash
grep -o -i "[0-9a-f]\{8\}-[0-9a-f]\{4\}-[0-9a-f]\{4\}-[0-9a-f]\{4\}-[0-9a-f]\{12\}" branding/updater.gni | sort | uniq -d
```

Expected: no output, so no GUID repeats.

```bash
for g in $(grep -o -i "[0-9a-f]\{8\}-[0-9a-f-]\{27\}" branding/updater.gni); do grep -qi "$g" chromium/src/chrome/updater/branding.gni && echo "upstream: $g"; done
```

Expected: no output.

- [ ] **Step 2: The browser's app ID.**

```bash
grep -n 'browser_appid' branding/updater.gni
```

Set `.app_guid` in `branding/install_modes.h` to that value, in the same braces form, replacing the empty value and its "Empty until the updater exists" comment:

```cpp
        // Must equal browser_appid in branding/updater.gni: the key the
        // updater finds this browser under.
        .app_guid = L"{BROWSER_APPID}",
```

Add to `tools/tests/test_update_server.py`:

```python
class IdentityTest(unittest.TestCase):
    def test_browser_appid_matches_the_install_mode(self):
        gni = (repo.REPO_ROOT / "branding" / "updater.gni").read_text(encoding="utf-8")
        modes = (repo.REPO_ROOT / "branding" / "install_modes.h").read_text(encoding="utf-8")
        appid = re.search(r'browser_appid = "([^"]+)"', gni).group(1)
        self.assertIn(f'.app_guid = L"{appid}"', modes)
        company = re.search(r'updater_company_short_name = "([^"]+)"', gni).group(1)
        self.assertIn(f'kCompanyPathName[] = L"{company}"', modes)
```

Also add `import re` at the top.

- [ ] **Step 3: Turn the updater on.** Append to `build/args/dev.gn`:

```gn
# Ghost's updater (Phase 2, sub-project B). Off by default outside Chrome
# builds; the second turns on the browser's "relaunch to update" prompt.
enable_updater = true
enable_update_notifications = true
```

- [ ] **Step 4: Patch 0015.** In `$SRC/chrome/updater/branding.gni`, change the line `} else {` (the start of the Chromium branch) to:

```gn
} else if (false) {  # Ghost: replaced by //ghost/branding/updater.gni below.
```

Append at the end of the file:

```gn

# Ghost: the updater's identity (test identity until the final name).
if (!is_chrome_branded) {
  import("//ghost/branding/updater.gni")
}
```

- [ ] **Step 5: Commit both sides, then build the updater.**

```bash
cd $WEBOPS && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "branding: the updater's test identity

branding/updater.gni gives Ghost's updater its names, fresh GUIDs, the
loopback update URL and no Google endpoints (crash, event logging, logos,
help). The browser's install mode gets its app ID. dev.gn turns the
updater and update notifications on."
git -C $SRC commit -q -as -m "updater: take the branding from //ghost

Ghost's updater has its own names, GUIDs and endpoints. The Chromium
branch would point the updater at Google's QA server and send crash
reports and event logs to Google.

Why: branding.gni knows only Google Chrome and Chromium.
Upstream: not upstreamable: product-specific branding"
python tools/patches.py export --src chromium/src && git add patches && python tools/lint.py \
  && git commit -q -m "patches: the updater takes its branding from //ghost"
git -C $SRC/ghost pull --ff-only
cd $SRC && time cmd //c "autoninja.bat -C out\\vanilla -j 10 chrome/updater/win:updater chrome/updater/win/installer:installer chrome/updater/win:signing mini_installer" 2>&1 | tail -2
ls out/vanilla/updater.exe out/vanilla/UpdaterSetup.exe out/vanilla/UpdaterSigning/
```

Expected: `updater.exe` and `UpdaterSetup.exe` exist, and `UpdaterSigning` holds `sign.py`, `resedit.py`, `7zr.exe` and `tag.exe`. Run it in the background; note the time. If GN reports an undefined identifier from `branding.gni`, upstream defines a variable this plan's script didn't see. Add it to `branding/updater.gni` with a fresh value, and record it in the spike notes.

---

### Task 7: Registration with Ghost's updater

**Files:**
- Modify (patch 0016): `$SRC/chrome/install_static/BUILD.gn`, `$SRC/chrome/install_static/install_modes.cc`
- Modify (patch 0017): `$SRC/chrome/browser/updater/browser_updater_client_win.cc`, `$SRC/chrome/browser/updater/BUILD.gn`

- [ ] **Step 1: Patch 0016.** In `$SRC/chrome/install_static/BUILD.gn`, `if (is_chrome_branded && is_win) {` becomes:

```gn
  # Ghost: the browser registers with Ghost's updater on Windows.
  if (is_win) {
```

In `$SRC/chrome/install_static/install_modes.cc`, inside the `#if BUILDFLAG(USE_GOOGLE_UPDATE_INTEGRATION)` block of the anonymous namespace, the three functions that build Google Update paths become the company's (the new helper goes in the same block):

```cpp
std::wstring GetUpdaterKeyPath(const wchar_t* subkey, const wchar_t* app_guid) {
  // Ghost: Ghost's updater keeps its keys under the company directory
  // (branding/install_modes.h, branding/updater.gni).
  return std::wstring(L"Software\\")
      .append(kCompanyPathName)
      .append(L"\\Update\\")
      .append(subkey)
      .append(L"\\")
      .append(app_guid);
}

std::wstring GetClientsKeyPathForApp(const wchar_t* app_guid) {
  return GetUpdaterKeyPath(L"Clients", app_guid);
}

std::wstring GetClientStateKeyPathForApp(const wchar_t* app_guid) {
  return GetUpdaterKeyPath(L"ClientState", app_guid);
}

std::wstring GetClientStateMediumKeyPathForApp(const wchar_t* app_guid) {
  return GetUpdaterKeyPath(L"ClientStateMedium", app_guid);
}
```

Then search for other Google Update paths the flag now compiles in:

```bash
cd $SRC && git grep -n 'Google\\\\Update' -- chrome/install_static chrome/installer/util chrome/installer/setup | grep -v test
```

Every hit becomes an entry in Task 13's audit. Patch a hit only if it decides where the browser registers.

```bash
git -C $SRC commit -q -as -m "install_static: register with Ghost's updater

The browser records its version and install result where Ghost's updater
reads them: Software\\<company>\\Update\\Clients\\{app_guid} and its
ClientState keys.

Why: USE_GOOGLE_UPDATE_INTEGRATION is tied to is_chrome_branded, and the
key paths name Google.
Upstream: not upstreamable: product-specific updater integration"
```

- [ ] **Step 2: Patch 0017.** In `$SRC/chrome/browser/updater/browser_updater_client_win.cc`:
- `req.version = version_info::GetVersionNumber();` becomes `req.version = ghost::ReleaseVersion().GetString();`;
- add `#include "ghost/version/version.h"` in sorted position.

In `$SRC/chrome/browser/updater/BUILD.gn`, add `"//ghost/version",` to the deps of the target that lists `browser_updater_client_win.cc`. If `version_info` has no other use in the file, remove its include.

```bash
git -C $SRC commit -q -as -m "updater: register the browser's release version

The updater compares the registered version with what it installed, the
release version (ADR 0007). version_info reports the Chromium release.

Why: the registration reads version_info directly.
Upstream: not upstreamable: product-specific versioning"
cd $WEBOPS && python tools/patches.py export --src chromium/src && git add patches && python tools/lint.py \
  && git commit -q -m "patches: the browser registers with Ghost's updater at its release version"
```

- [ ] **Step 3: Build.**

```bash
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 chrome mini_installer ghost_unittests" 2>&1 | tail -1
```

Expected: success. Turning the flag on recompiles `install_static` and its users, which is large: run it in the background.

---

### Task 8: The CRX publisher format

**Files:**
- Create: `updater/crx_verifier_unittest.cc`, `test/updater/data/ghost_publisher.crx3`, `test/updater/data/other_publisher.crx3`
- Modify: `BUILD.gn` (`ghost_unittests`)
- Modify (patch 0018): `$SRC/components/crx_file/crx_verifier.h`, `crx_verifier.cc`, `BUILD.gn`
- Modify (patch 0019): `$SRC/chrome/updater/external_constants_default.cc`

- [ ] **Step 1: The fixtures.**

```bash
cd $WEBOPS/tools && mkdir -p ../test/updater/data && python - <<'EOF'
from pathlib import Path
import crx3, ecdsa_p256, update_server
files = {"payload.txt": b"Ghost CRX verifier fixture\n"}
out = Path("../test/updater/data")
(out / "ghost_publisher.crx3").write_bytes(crx3.build(files, update_server.load_key(update_server.CRX_KEY_FILE)))
(out / "other_publisher.crx3").write_bytes(crx3.build(files, 0xC9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721))
EOF
```

`other_publisher.crx3` is signed with RFC 6979's published key, which is public on purpose. Add a Python test so the fixture can't drift from the key:

```python
class FixtureTest(unittest.TestCase):
    def test_ghost_fixture_is_signed_by_the_test_publisher_key(self):
        data = (DATA / "data" / "ghost_publisher.crx3").read_bytes()
        key = ec.spki(ec.public_key(us.load_key(us.CRX_KEY_FILE)))
        self.assertEqual(crx3.verified_keys(data), [key])
```

Add it to `tools/tests/test_update_server.py`.

- [ ] **Step 2: The C++ test.** Create `updater/crx_verifier_unittest.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// patches/0018: CRX3_WITH_GHOST_PUBLISHER_PROOF accepts only Ghost's
// publisher key, and the Web Store format still requires Google's.

#include "components/crx_file/crx_verifier.h"

#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

base::FilePath Fixture(const char* name) {
  return base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
      .AppendASCII("ghost/test/updater/data")
      .AppendASCII(name);
}

crx_file::VerifierResult Verify(const char* name,
                                crx_file::VerifierFormat format) {
  std::string public_key, crx_id;
  std::vector<uint8_t> verified_contents;
  return crx_file::Verify(Fixture(name), format, {}, {}, &public_key, &crx_id,
                          &verified_contents);
}

TEST(CrxVerifierTest, GhostFormatAcceptsGhostsPublisher) {
  EXPECT_EQ(Verify("ghost_publisher.crx3",
                   crx_file::VerifierFormat::CRX3_WITH_GHOST_PUBLISHER_PROOF),
            crx_file::VerifierResult::OK_FULL);
}

TEST(CrxVerifierTest, GhostFormatRejectsOtherPublishers) {
  EXPECT_EQ(Verify("other_publisher.crx3",
                   crx_file::VerifierFormat::CRX3_WITH_GHOST_PUBLISHER_PROOF),
            crx_file::VerifierResult::ERROR_REQUIRED_PROOF_MISSING);
}

TEST(CrxVerifierTest, WebStoreFormatStillRequiresGooglesPublisher) {
  EXPECT_EQ(Verify("ghost_publisher.crx3",
                   crx_file::VerifierFormat::CRX3_WITH_PUBLISHER_PROOF),
            crx_file::VerifierResult::ERROR_REQUIRED_PROOF_MISSING);
}

}  // namespace
}  // namespace ghost
```

In `BUILD.gn`, add to `ghost_unittests`:
- `"updater/crx_verifier_unittest.cc",` to `sources`;
- `"//components/crx_file",` to `deps`;
- a `data = [ "//ghost/test/updater/data/" ]` line.

- [ ] **Step 3: Build; it fails to compile** (no `CRX3_WITH_GHOST_PUBLISHER_PROOF` yet).

```bash
cd $WEBOPS && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "updater: test the CRX publisher format Ghost's updater requires"
git -C $SRC/ghost pull --ff-only
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 ghost_unittests" 2>&1 | grep -m1 -E "error|CRX3_WITH_GHOST"
```

- [ ] **Step 4: Patch 0018.** In `$SRC/components/crx_file/crx_verifier.h`, add to `VerifierFormat` after `CRX3_WITH_PUBLISHER_PROOF,`:

```cpp
  CRX3_WITH_GHOST_PUBLISHER_PROOF,  // Ghost: accept only Crx3 with Ghost's
                                    // publisher proof (Ghost's updater).
```

In `$SRC/components/crx_file/crx_verifier.cc`:
- add `#include "ghost/branding/crx_publisher_key.h"` in sorted position;
- `VerifyCrx3` gains a parameter, `bool accept_only_ghost_publisher_key`, after `accept_publisher_test_key`. The `found_publisher_key` statement becomes:

  ```cpp
      found_publisher_key =
          found_publisher_key ||
          (accept_only_ghost_publisher_key
               ? key_hash == ghost::kCrxPublisherKeyHash
               : (key_hash == kPublisherKeyHash ||
                  (accept_publisher_test_key &&
                   key_hash == kPublisherTestKeyHash)));
  ```

- in `Verify`:
  - `require_publisher_key` also holds for `CRX3_WITH_GHOST_PUBLISHER_PROOF`;
  - the call passes `format == VerifierFormat::CRX3_WITH_GHOST_PUBLISHER_PROOF` as the new argument.

In `$SRC/components/crx_file/BUILD.gn`, add `"//ghost/branding:keys"` to the deps of the target that compiles `crx_verifier.cc`.

Search for `switch` statements over `VerifierFormat` that now need the new value: `git -C $SRC grep -n "VerifierFormat::CRX3_WITH_PUBLISHER_PROOF" -- '*.cc' | grep -v test`. Each must handle the new value, or not need to (an `if` comparing to one value doesn't).

```bash
git -C $SRC commit -q -as -m "crx_file: add a format that requires Ghost's publisher proof

CRX3_WITH_GHOST_PUBLISHER_PROOF accepts a CRX3 only with a proof by the
key whose hash is in //ghost/branding/crx_publisher_key.h. The existing
formats, including the Web Store's, are unchanged.

Why: the publisher key is a constant in crx_verifier.cc.
Upstream: not upstreamable: product-specific signing key"
```

- [ ] **Step 5: Patch 0019.** In `$SRC/chrome/updater/external_constants_default.cc`, `return crx_file::VerifierFormat::CRX3_WITH_PUBLISHER_PROOF;` becomes:

```cpp
    // Ghost: updates must carry Ghost's publisher proof, made with a key kept
    // off the update server (docs/threat-model.md).
    return crx_file::VerifierFormat::CRX3_WITH_GHOST_PUBLISHER_PROOF;
```

```bash
git -C $SRC commit -q -as -m "updater: require Ghost's publisher proof on updates

Why: the updater's CRX format is a constant.
Upstream: not upstreamable: product-specific signing key"
cd $WEBOPS && python tools/patches.py export --src chromium/src && git add patches && python tools/lint.py \
  && git commit -q -m "patches: Ghost's updater requires Ghost's CRX publisher proof"
```

- [ ] **Step 6: Build and run.**

```bash
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 ghost_unittests chrome/updater/win:updater" 2>&1 | tail -1
out/vanilla/ghost_unittests.exe --gtest_filter='CrxVerifierTest.*' 2>&1 | grep -E "SUCCESS|FAILED"
```

Expected: `SUCCESS` (3 tests).

---

### Task 9: CUP with Ghost's key, and the request scrubber

**Files:**
- Create: `updater/cup_unittest.cc`, `components/update_client/BUILD.gn`, `components/update_client/request_scrubber.h`, `request_scrubber.cc`, `request_scrubber_unittest.cc`
- Modify: `BUILD.gn` (`ghost_unittests`)
- Modify (patch 0020): `$SRC/components/update_client/request_sender.cc`, `BUILD.gn`
- Modify (patch 0021): `$SRC/components/update_client/protocol_serializer_json.cc`, `BUILD.gn`
- Modify (patch 0022): `$SRC/components/update_client/ping_manager.cc`

- [ ] **Step 1: The CUP interoperability test.** Create `updater/cup_unittest.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Chromium's CUP verifier accepts what tools/update_server.py signs with the
// test key, and nothing else (test/updater/cup_vector.json).

#include "components/client_update_protocol/cup.h"

#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/values.h"
#include "ghost/branding/cup_key.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

base::DictValue Vector() {
  std::string text;
  CHECK(base::ReadFileToString(
      base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
          .AppendASCII("ghost/test/updater/cup_vector.json"),
      &text));
  return std::move(*base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC));
}

TEST(CupTest, ChromiumAcceptsTheTestServersProof) {
  const base::DictValue vector = Vector();
  client_update_protocol::Cup cup(kCupKeyVersion, kCupPublicKey);
  cup.PrepareRequestParameters(*vector.FindString("request"));
  cup.OverrideNonceForTesting(kCupKeyVersion, *vector.FindInt("nonce"));
  EXPECT_TRUE(cup.ValidateResponse(*vector.FindString("response"),
                                   *vector.FindString("proof")));
}

TEST(CupTest, ChromiumRejectsAnAlteredResponse) {
  const base::DictValue vector = Vector();
  client_update_protocol::Cup cup(kCupKeyVersion, kCupPublicKey);
  cup.PrepareRequestParameters(*vector.FindString("request"));
  cup.OverrideNonceForTesting(kCupKeyVersion, *vector.FindInt("nonce"));
  EXPECT_FALSE(cup.ValidateResponse(*vector.FindString("response") + " ",
                                    *vector.FindString("proof")));
}

}  // namespace
}  // namespace ghost
```

- [ ] **Step 2: The scrubber's interface and golden test.** Create `components/update_client/request_scrubber.h`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_UPDATE_CLIENT_REQUEST_SCRUBBER_H_
#define GHOST_COMPONENTS_UPDATE_CLIENT_REQUEST_SCRUBBER_H_

#include "base/values.h"

namespace ghost {

// Removes every key the privacy model doesn't allow from a serialized update
// request ({"request": {...}}), including keys a later milestone adds. The
// allow-list is in docs/privacy-model.md and tools/update_server.py; both
// are tested against test/updater/request_*.json. Called by
// ProtocolSerializerJSON::Serialize (patches/0021).
void ScrubUpdateRequest(base::DictValue& root);

}  // namespace ghost

#endif  // GHOST_COMPONENTS_UPDATE_CLIENT_REQUEST_SCRUBBER_H_
```

Create `components/update_client/request_scrubber_unittest.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/update_client/request_scrubber.h"

#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

base::DictValue Golden(const char* name) {
  std::string text;
  CHECK(base::ReadFileToString(
      base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
          .AppendASCII("ghost/test/updater")
          .AppendASCII(name),
      &text));
  return std::move(*base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC));
}

TEST(RequestScrubberTest, KeepsExactlyTheAllowList) {
  base::DictValue request = Golden("request_all_fields.json");
  ScrubUpdateRequest(request);
  EXPECT_EQ(request, Golden("request_scrubbed.json"));
}

TEST(RequestScrubberTest, LeavesAnAllowedRequestUnchanged) {
  base::DictValue request = Golden("request_scrubbed.json");
  ScrubUpdateRequest(request);
  EXPECT_EQ(request, Golden("request_scrubbed.json"));
}

TEST(RequestScrubberTest, DropsAnythingButTheRequest) {
  base::DictValue root;
  root.Set("request", base::DictValue());
  root.Set("other", 1);
  ScrubUpdateRequest(root);
  EXPECT_FALSE(root.Find("other"));
}

}  // namespace
}  // namespace ghost
```

Create `components/update_client/BUILD.gn`:

```gn
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Linked into //components/update_client (patches/0021), so it may depend on
# //base only.
source_set("request_scrubber") {
  sources = [
    "request_scrubber.cc",
    "request_scrubber.h",
  ]
  deps = [ "//base" ]
}
```

In `BUILD.gn`, add to `ghost_unittests`:
- sources `"components/update_client/request_scrubber_unittest.cc"` and `"updater/cup_unittest.cc"`;
- deps `"//components/client_update_protocol"`, `"//ghost/branding:keys"` and `"//ghost/components/update_client:request_scrubber"`;
- extend `data` with `"//ghost/test/updater/"`.

- [ ] **Step 3: Commit; the build fails to link** (`ScrubUpdateRequest` is undefined). `CupTest` doesn't depend on patch 0020: it builds Chromium's verifier with `kCupPublicKey` directly.

```bash
cd $WEBOPS && git add -A && python tools/lint.py && git commit -q -m "update_client: test the request scrubber and CUP with Ghost's key"
git -C $SRC/ghost pull --ff-only
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 ghost_unittests" 2>&1 | grep -m2 -E "error|undefined|unresolved"
```

- [ ] **Step 4: Implement.** Create `components/update_client/request_scrubber.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/update_client/request_scrubber.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/contains.h"

namespace ghost {
namespace {

using Keys = std::initializer_list<std::string_view>;

void KeepOnly(base::DictValue& dict, Keys keys) {
  std::vector<std::string> drop;
  for (const auto [key, value] : dict) {
    if (!base::Contains(keys, key)) {
      drop.push_back(key);
    }
  }
  for (const std::string& key : drop) {
    dict.Remove(key);
  }
}

void KeepOnlyInEach(base::DictValue& parent, std::string_view list, Keys keys) {
  if (base::ListValue* entries = parent.FindList(list)) {
    for (base::Value& entry : *entries) {
      if (entry.is_dict()) {
        KeepOnly(entry.GetDict(), keys);
      }
    }
  }
}

}  // namespace

void ScrubUpdateRequest(base::DictValue& root) {
  KeepOnly(root, {"request"});
  base::DictValue* request = root.FindDict("request");
  if (!request) {
    return;
  }
  KeepOnly(*request, {"protocol", "ismachine", "acceptformat", "sessionid",
                      "requestid", "@updater", "updaterversion", "prodversion",
                      "updaterchannel", "prodchannel", "@os", "arch", "wow64",
                      "dlpref", "os", "apps"});
  if (base::DictValue* os = request->FindDict("os")) {
    KeepOnly(*os, {"platform", "arch", "version"});
  }
  base::ListValue* apps = request->FindList("apps");
  if (!apps) {
    return;
  }
  for (base::Value& value : *apps) {
    if (!value.is_dict()) {
      continue;
    }
    base::DictValue& app = value.GetDict();
    KeepOnly(app, {"appid", "version", "ap", "brand", "release_channel",
                   "enabled", "disabled", "cached_items", "updatecheck",
                   "data"});
    KeepOnlyInEach(app, "disabled", {"reason"});
    KeepOnlyInEach(app, "cached_items", {"sha256"});
    KeepOnlyInEach(app, "data", {"name", "index"});
    if (base::DictValue* check = app.FindDict("updatecheck")) {
      KeepOnly(*check, {"updatedisabled", "rollback_allowed",
                        "sameversionupdate", "targetversionprefix"});
    }
  }
}

}  // namespace ghost
```

- [ ] **Step 5: Build and run.** `RequestScrubberTest.*` and `CupTest.*` pass.

```bash
cd $WEBOPS && git add -A && python tools/lint.py && git commit -q -m "update_client: the allow-list request scrubber

Keeps only the keys docs/privacy-model.md allows in an update request,
including against keys a later milestone adds; checked against the same
golden pair as tools/update_server.py."
git -C $SRC/ghost pull --ff-only
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 ghost_unittests" 2>&1 | tail -1
out/vanilla/ghost_unittests.exe --gtest_filter='RequestScrubberTest.*:CupTest.*' 2>&1 | grep -E "SUCCESS|FAILED"
```

Expected: `SUCCESS` (5 tests).

- [ ] **Step 6: Mutation check the scrubber.** Each must fail `RequestScrubberTest`. Rebuild `ghost_unittests` after each and restore:
- drop `KeepOnly(*os, …)`;
- drop `KeepOnlyInEach(app, "data", …)`;
- add `"ping"` to the app keys.

- [ ] **Step 7: Patch 0020, CUP.** In `$SRC/components/update_client/request_sender.cc`:
- add `#include "ghost/branding/cup_key.h"`;
- `BuildSigner()` becomes:

  ```cpp
  client_update_protocol::Cup BuildSigner() {
    // Ghost: Ghost's update server signs with its own ECDSA key
    // (//ghost/branding/cup_key.h). Post-quantum CUP waits for Ghost's
    // production keys, so kPqcCupSigning is not consulted.
    return client_update_protocol::Cup(ghost::kCupKeyVersion,
                                       base::as_byte_span(ghost::kCupPublicKey));
  }
  ```

- If `kEcdsaKeyVersion`, `kEcdsaPublicKey`, `kMldsa44KeyVersion`, `kMldsa44PublicKey` and the `features.h` include become unused, the compiler reports unused constants: delete them in the same patch.

In `$SRC/components/update_client/BUILD.gn`, add `"//ghost/branding:keys"` to `update_client`'s deps.

```bash
git -C $SRC commit -q -as -m "update_client: sign-check update responses with Ghost's CUP key

Why: the CUP public key is a constant in request_sender.cc.
Upstream: not upstreamable: product-specific signing key"
```

- [ ] **Step 8: Patch 0021, the scrubber.** In `$SRC/components/update_client/protocol_serializer_json.cc`:
- add `#include "ghost/components/update_client/request_scrubber.h"`;
- right before `std::string msg;` at the end of `Serialize`, add:

  ```cpp
    // Ghost: only the keys docs/privacy-model.md allows leave the machine.
    ghost::ScrubUpdateRequest(root_node);
  ```

In `$SRC/components/update_client/BUILD.gn`, add `"//ghost/components/update_client:request_scrubber"` to `update_client`'s deps.

```bash
git -C $SRC commit -q -as -m "update_client: scrub update requests to Ghost's allow-list

Why: the serializer writes install IDs, activity counters and hardware
details with no embedder control.
Upstream: not upstreamable: product-specific privacy policy"
```

- [ ] **Step 9: Patch 0022, no event requests.** In `$SRC/components/update_client/ping_manager.cc`, at the start of `PingManager::SendPing`'s body, after `DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);`:

```cpp
  // Ghost: no event requests (docs/privacy-model.md). The branch below
  // returns without sending.
  events.clear();
```

```bash
git -C $SRC commit -q -as -m "update_client: send no event requests

Why: events are sent unconditionally after every update operation.
Upstream: not upstreamable: product-specific privacy policy"
cd $WEBOPS && python tools/patches.py export --src chromium/src && git add patches && python tools/lint.py \
  && git commit -q -m "patches: Ghost's CUP key, the request allow-list, no event requests"
```

- [ ] **Step 10: Build everything and run the suites.** Run in the background:

```bash
cd $SRC && time cmd //c "autoninja.bat -C out\\vanilla -j 10 chrome ghost_unittests ghost_browsertests mini_installer chrome/updater/win:updater chrome/updater/win/installer:installer chrome/updater/win:signing" 2>&1 | tail -1
out/vanilla/ghost_unittests.exe 2>&1 | tail -2; out/vanilla/ghost_browsertests.exe 2>&1 | tail -2
```

Expected: `SUCCESS` twice: 25 unit tests (17 + 3 + 2 + 3) and 15 browser tests.

---

### Task 10: The end-to-end test

**Files:**
- Create: `tools/update_smoke.py`, `tools/tests/test_update_smoke.py`
- Modify: `tools/installer_smoke.py` (`sandbox_config` takes the script line)

- [ ] **Step 1: Let `sandbox_config` run other scripts.** In `tools/installer_smoke.py`:
- `sandbox_config` gains a last parameter `script_args: str | None = None`.
- When it is `None`, the existing installer line is used. Otherwise the command becomes:

  ```python
  f'cmd.exe /c "{_IN_SANDBOX["python"]}\\python.exe {_IN_SANDBOX["tools"]}\\{script_args} '
  f'> {_IN_SANDBOX["results"]}\\run.log 2>&1 & shutdown /s /t 0"'
  ```

- Add a test in `test_installer_smoke.py` that `script_args="update_smoke.py run --x"` appears in the command, and that the default still names `installer_smoke.py run`.

- [ ] **Step 2: Write the evaluation tests first.** Create `tools/tests/test_update_smoke.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import unittest

import update_smoke


class RequestLogTest(unittest.TestCase):
    def test_allowed_requests_pass(self):
        lines = [json.dumps({"path": "/update?cup2key=1:2",
                             "body": {"request": {"protocol": "4.0", "apps": []}}})]
        self.assertEqual(update_smoke.evaluate_requests(lines), [])

    def test_forbidden_keys_and_no_requests_fail(self):
        lines = [json.dumps({"path": "/update",
                             "body": {"request": {"protocol": "4.0", "hw": {}}}})]
        self.assertEqual(update_smoke.evaluate_requests(lines),
                         ["request 1 carries request.hw"])
        self.assertEqual(update_smoke.evaluate_requests([]), ["the updater sent no request"])


class UpdaterLogTest(unittest.TestCase):
    def test_only_loopback_urls(self):
        log = "x http://127.0.0.1:8484/update y\nz https://example.com/a\n"
        self.assertEqual(update_smoke.foreign_urls(log), ["https://example.com/a"])


if __name__ == "__main__":
    unittest.main()
```

Run it; it fails (`No module named 'update_smoke'`).

- [ ] **Step 3: Implement.** Create `tools/update_smoke.py`:

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Updater end to end in Windows Sandbox (Phase 2, sub-project B).

  sandbox --offline-installer O --release-version V1 --update-crx C
          --update-version V2 --appid A
      on the build machine: run the test in a fresh Windows Sandbox
  run     the test itself, in the sandbox

The test installs Ghost from the offline installer (V1), lets the updater
take the update from tools/update_server.py (V2, a CRX3), checks every
request against the allow-list and the updater's log for other hosts, then
uninstalls the browser and checks the updater removes itself.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import replace
from pathlib import Path

import installer_smoke as smoke
import offline_installer
import repo
import update_server

TOOLS_DIR = Path(__file__).resolve().parent
RESULT_FILE = smoke.RESULT_FILE
EXPECTATIONS_FILE = smoke.EXPECTATIONS_FILE
_URL_RE = re.compile(r"https?://[^\s\"'<>]+")


def evaluate_requests(lines: list[str]) -> list[str]:
    failures = []
    for i, line in enumerate(lines, 1):
        body = json.loads(line)["body"] or {}
        failures += [f"request {i} carries {key}" for key in update_server.disallowed_keys(body)]
    return failures or ([] if lines else ["the updater sent no request"])


def foreign_urls(log: str) -> list[str]:
    return [url for url in _URL_RE.findall(log) if not url.startswith("http://127.0.0.1")]


def _company_dir(exp: smoke.Expectations) -> Path:
    return Path(os.environ["LOCALAPPDATA"]) / exp.company_path


def _updater_exe(exp: smoke.Expectations) -> Path | None:
    found = sorted(_company_dir(exp).glob("ProjectGhostUpdater/*/updater.exe"))
    return found[-1] if found else None


def _registered_version(exp: smoke.Expectations, appid: str) -> str | None:
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                            rf"Software\{exp.company_path}\Update\Clients\{appid}") as key:
            return winreg.QueryValueEx(key, "pv")[0]
    except OSError:
        return None


def _updater_key_exists(exp: smoke.Expectations) -> bool:
    import winreg
    try:
        winreg.OpenKey(winreg.HKEY_CURRENT_USER, rf"Software\{exp.company_path}\Update").Close()
        return True
    except OSError:
        return False


def _updater_tasks(exp: smoke.Expectations) -> list[str]:
    out = subprocess.run(["schtasks", "/query", "/v", "/fo", "csv"], capture_output=True,
                         text=True).stdout
    return [line for line in out.splitlines() if "ProjectGhostUpdater" in line]


def _wait(condition, seconds: int) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(5)
    return condition()


def run(payload: Path, results: Path, exp: smoke.Expectations, appid: str,
        update_version: str) -> dict:
    result = {"expectations": exp.__dict__, "steps": []}
    log = results / "requests.jsonl"
    updated = replace(exp, release_version=update_version)

    def step(name: str, failures: list[str], **details) -> bool:
        result["steps"].append({"name": name, "failures": failures, **details})
        return not failures

    server = update_server.UpdateServer(
        ("127.0.0.1", 8484),
        update_server.Offer(appid, update_version, payload / "update.crx3", "mini_installer.exe",
                            "--verbose-logging --do-not-launch-chrome"),
        update_server.load_key(payload / "cup_test_key.json"), log)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        if not step("clean machine", smoke.evaluate_uninstalled(smoke.snapshot(exp), exp)
                    + (["an updater is already installed"] if _updater_exe(exp) else [])):
            return result

        work = Path(tempfile.mkdtemp(prefix="offline-"))
        setup = Path(shutil.copy(payload / "ProjectGhostOfflineSetup.exe", work))
        code = subprocess.run([str(setup), *offline_installer.install_arguments(appid),
                               "--enable-logging"], timeout=900).returncode
        installed = smoke.snapshot(exp)
        failures = [] if code == 0 else [f"the offline installer exited with {code}"]
        failures += smoke.evaluate_installed(installed, exp)
        updater = _updater_exe(exp)
        failures += [] if updater else ["no updater.exe under the company directory"]
        failures += [] if _updater_tasks(exp) else ["no scheduled task for the updater"]
        pv = _registered_version(exp, appid)
        failures += [] if pv == exp.release_version else [
            f"Clients\\{appid} pv is {pv!r}, expected {exp.release_version!r}"]
        if not step("install", failures, snapshot=installed):
            return result

        code = subprocess.run([str(updater), "--update-apps", "--enable-logging"],
                              timeout=900).returncode
        reached = _wait(lambda: _registered_version(exp, appid) == update_version, 600)
        failures = [] if reached else [
            f"pv stayed {_registered_version(exp, appid)!r}, expected {update_version!r}"
            f" (updater exited with {code})"]
        app_dir = Path(installed["chrome_exe"]).parent
        failures += [] if (app_dir / update_version).is_dir() else [
            f"no {update_version} directory beside chrome.exe"]
        if not step("update", failures):
            return result
        step("launch", smoke.launch(Path(installed["chrome_exe"]), updated))

        lines = log.read_text(encoding="utf-8").splitlines() if log.exists() else []
        updater_log = "".join(p.read_text(encoding="utf-8", errors="replace")
                              for p in _company_dir(exp).rglob("updater*.log"))
        step("privacy", evaluate_requests(lines)
             + [f"the updater's log names {url}" for url in foreign_urls(updater_log)])

        setup_exe = app_dir / update_version / "Installer" / "setup.exe"
        code = subprocess.run([str(setup_exe), "--uninstall", "--force-uninstall",
                               "--verbose-logging"], timeout=900).returncode
        failures = ([] if code == smoke.UNINSTALL_SUCCESSFUL else
                    [f"setup.exe --uninstall exited with {code}"])
        subprocess.run([str(updater), "--uninstall-if-unused", "--enable-logging"], timeout=900)
        gone = _wait(lambda: _updater_exe(exp) is None, 300)
        uninstalled = smoke.snapshot(exp)
        failures += smoke.evaluate_uninstalled(uninstalled, exp)
        failures += [] if gone else ["the updater did not remove itself"]
        failures += ["the updater's scheduled task is left"] if _updater_tasks(exp) else []
        failures += ["Software\\…\\Update is left"] if _updater_key_exists(exp) else []
        step("uninstall", failures, snapshot=uninstalled)
    except Exception as e:  # reported, so the build machine learns why
        step("error", [f"{type(e).__name__}: {e}"])
    finally:
        server.shutdown()
        for name in ("chrome_installer.log",):
            path = Path(tempfile.gettempdir()) / name
            if path.exists():
                shutil.copy(path, results / name)
        for path in _company_dir(exp).rglob("updater*.log") if exp.company_path else []:
            shutil.copy(path, results / path.name)
        partial = results / (RESULT_FILE + ".partial")
        partial.write_text(json.dumps(result, indent=1), encoding="utf-8")
        partial.replace(results / RESULT_FILE)
    return result


def run_in_sandbox(offline: Path, crx: Path, release_version: str, update_version: str,
                   appid: str, timeout: int) -> int:
    payload = Path(tempfile.mkdtemp(prefix="update-payload-"))
    shutil.copy(offline, payload / "ProjectGhostOfflineSetup.exe")
    shutil.copy(crx, payload / "update.crx3")
    # Only tools/ is mapped into the sandbox; the server's key travels with the payload.
    shutil.copy(update_server.CUP_KEY_FILE, payload / "cup_test_key.json")
    results = Path(tempfile.mkdtemp(prefix="update-smoke-"))
    exp = smoke.expectations(repo.REPO_ROOT, release_version)
    (results / EXPECTATIONS_FILE).write_text(json.dumps(exp.__dict__), encoding="utf-8")
    script = (f"update_smoke.py run --payload {smoke._IN_SANDBOX['installer']} "
              f"--results {smoke._IN_SANDBOX['results']} --appid {appid} "
              f"--update-version {update_version}")
    config = results.with_suffix(".wsb")
    config.write_text(smoke.sandbox_config(payload, TOOLS_DIR, Path(sys.base_prefix), results,
                                           script_args=script), encoding="utf-8")
    print(f"starting Windows Sandbox; results in {results}", flush=True)
    subprocess.Popen([str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
                          / "WindowsSandbox.exe"), str(config)])
    deadline = time.monotonic() + timeout
    while not (results / RESULT_FILE).exists():
        if time.monotonic() > deadline:
            print(f"no result within {timeout}s", file=sys.stderr)
            return 1
        time.sleep(5)
    result = smoke.read_result(results)
    print(smoke.format_result(result))
    return 0 if smoke.passed(result) else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    s = sub.add_parser("sandbox")
    s.add_argument("--offline-installer", type=Path, required=True)
    s.add_argument("--release-version", required=True)
    s.add_argument("--update-crx", type=Path, required=True)
    s.add_argument("--update-version", required=True)
    s.add_argument("--appid", required=True)
    s.add_argument("--timeout", type=int, default=2700)
    r = sub.add_parser("run")
    r.add_argument("--payload", type=Path, required=True)
    r.add_argument("--results", type=Path, required=True)
    r.add_argument("--appid", required=True)
    r.add_argument("--update-version", required=True)
    args = parser.parse_args(argv)
    if args.command == "sandbox":
        return run_in_sandbox(args.offline_installer, args.update_crx, args.release_version,
                              args.update_version, args.appid, args.timeout)
    if not smoke.is_disposable(os.environ.get("USERNAME", ""), False):
        print("run installs into this user's profile; use `sandbox`", file=sys.stderr)
        return 2
    exp = smoke.Expectations(**json.loads(
        (args.results / EXPECTATIONS_FILE).read_text(encoding="utf-8")))
    result = run(args.payload, args.results, exp, args.appid, args.update_version)
    print(smoke.format_result(result))
    return 0 if smoke.passed(result) else 1


if __name__ == "__main__":
    sys.exit(main())
```

`_IN_SANDBOX["installer"]` is the read-only folder the payload is mapped to. The test copies the offline installer to a writable directory before running it, as `installer_smoke.py` does.

- [ ] **Step 4: Run the tooling tests; they pass. Commit.**

```bash
cd $WEBOPS && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "tools: the updater's end-to-end test in Windows Sandbox

Installs Ghost from the offline installer, lets the updater take an
update from the test server, checks each request against the allow-list
and the updater's log for other hosts, then uninstalls and checks the
updater removes itself."
```

- [ ] **Step 5: Build respin `-1` and respin `-2`, keeping their installers.** `S` is a scratch directory outside the repository:

```bash
S=$(mktemp -d) && echo $S && APPID=$(grep -o 'browser_appid = "[^"]*"' $WEBOPS/branding/updater.gni | cut -d'"' -f2)
cd $WEBOPS && python tools/release_version.py write --src chromium/src --tag 152.0.7977.149-1
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 mini_installer chrome/updater/win/installer:installer chrome/updater/win:signing" 2>&1 | tail -1
cd $WEBOPS && python tools/offline_installer.py --src chromium/src --out out/vanilla \
  --version 152.0.7977.14901 --appid "$APPID" --output "$S/ProjectGhostOfflineSetup.exe"
git -C $SRC checkout -- chrome/VERSION
python tools/release_version.py write --src chromium/src --tag 152.0.7977.149-2
cd $SRC && cmd //c "autoninja.bat -C out\\vanilla -j 10 mini_installer" 2>&1 | tail -1
cd $WEBOPS/tools && python update_server.py crx --installer ../chromium/src/out/vanilla/mini_installer.exe --out "$S/update.crx3"
git -C $SRC checkout -- chrome/VERSION && ls -l $S
```

Expected: `ProjectGhostOfflineSetup.exe` and `update.crx3` in `$S`. Run each build in the background; each is about 12 minutes.

- [ ] **Step 6: Run the end-to-end test.**

```bash
cd $WEBOPS && python tools/update_smoke.py sandbox --offline-installer "$S/ProjectGhostOfflineSetup.exe" \
  --release-version 152.0.7977.14901 --update-crx "$S/update.crx3" --update-version 152.0.7977.14902 --appid "$APPID"
```

Expected: `ok` for clean machine, install, update, launch, privacy and uninstall, then `PASSED`.

**If a step fails, read the evidence in the results directory before changing anything:** `run.log`, `result.json`, `requests.jsonl`, `updater*.log` and `chrome_installer.log`. Fix the cause in the right place:
- **A runtime detail of this test** (a task name, a log path, an exit code): fix it in `update_smoke.py`.
- **An identity value:** fix it in `branding/updater.gni`.
- **A hook:** fix it in its patch.

Record each failure and its fix in the spike notes. Re-run until it passes.

---

### Task 11: Audit of `USE_GOOGLE_UPDATE_INTEGRATION` and the updater's UI

- [ ] **Step 1: List what the flag compiles in.**

```bash
cd $SRC && git grep -n "USE_GOOGLE_UPDATE_INTEGRATION" -- '*.cc' '*.h' | grep -v test
git grep -n 'Google\\\\Update\|GoogleUpdate' -- chrome/install_static chrome/installer/util chrome/installer/setup chrome/browser/*.cc | grep -v test | head -40
```

For each hit, read the code it guards and write an entry in the spike notes. Each entry says what it does and one of:
- **turned off,** with the patch (when it sends data or reaches Google);
- **kept,** with the reason it's harmless.

Expected entries include the `dr` (did run) value, brand codes, `usagestats` consent, and the Google Update metrics provider.

- [ ] **Step 2: The updater's UI strings.** Install the offline installer again in Sandbox without `--silent`, with a screenshot step, or read the string table directly. Record whether any text shows "Chromium" or "Google":

```bash
cd $SRC && grep -o "IDS_[A-Z_]*\" desc=\"[^\"]*\"" chrome/updater/app/server/win/updater_strings.grd 2>/dev/null | head
```

A string that names another product is fixed in this sub-project only if it shows during a per-user install. Otherwise it's listed for the final-name step.

---

### Task 12: Mutation checks of the end-to-end test

Each change below is made, the affected target rebuilt, the end-to-end test re-run (with the same `$S` inputs where the change doesn't affect them), and the change undone. Each must fail in the step named:

| Change | Rebuild or rebuild input | Must fail |
|---|---|---|
| The payload's `cup_test_key.json` is replaced by a fresh key (in `run_in_sandbox`, after the copy, write `update_server._write_key(payload / "cup_test_key.json", ecdsa_p256.generate_private_key(), "mutation")`) | none | `update`: `pv` stays `…14901` |
| `update.crx3` built with another key: `update_server.py crx --key <file with RFC 6979's key>` | `$S/update.crx3` | `update` |
| Patch 0021 reverted in `$SRC` | `updater`, `UpdaterSetup`, offline installer | `privacy`: forbidden keys in `requests.jsonl` |
| Patch 0022 reverted | the same | `privacy`: a request carrying `events` |
| Patch 0016's key path reverted (Google's path) | `mini_installer`, offline installer | `install`: `Clients\{appid} pv` missing |

Revert patches the way sub-project A's Task 10 did: `git show <commit> -- <paths> | git apply -R`, then `git checkout -- <paths>`. Record each result in the spike notes.

---

### Task 13: Spike notes and documentation

**Files:**
- Create: `docs/superpowers/specs/2026-10-02-branded-updater-spike.md`
- Modify: `docs/privacy-model.md`, `docs/architecture.md`, `docs/build/windows.md`, `docs/roadmap.md`, the spec (status)

- [ ] **Step 1: Spike notes.** Record:
- build times: the first updater build, the build after `USE_GOOGLE_UPDATE_INTEGRATION`, each respin;
- Task 5's layout result;
- every end-to-end failure and its fix;
- the audit (Task 11);
- the mutation results (Task 12);
- a real update request from `requests.jsonl`, after scrubbing, for the privacy model.

- [ ] **Step 2: `docs/privacy-model.md`.** Under "Data the browser sends", replace "The exact request format will be published with the updater in Phase 2." with:
  - the allow-list table, as in the spec;
  - the captured example request;
  - that `sessionid` is random per update session and never stored;
  - that no event requests are sent;
  - that the request headers name the updater and the app (`X-Goog-Update-*`).

- [ ] **Step 3: `docs/architecture.md`, "Updates and signed data":** the updater as built.
  - The company layout.
  - The trust chain: CUP with Ghost's key for responses; Ghost's publisher proof for packages.
  - The scrubber.
  - The test identity.

- [ ] **Step 4: `docs/build/windows.md`.** Add a section, "Offline installer", with the commands from Task 10 Step 5.

- [ ] **Step 5: Roadmap.** Mark sub-project B done, with the end-to-end result, and link the spec and the spike notes. Set the spec's status to implemented.

- [ ] **Step 6: Commit; ask before pushing.**

```bash
cd $WEBOPS && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "docs: Ghost's updater, as built

The privacy model publishes the exact update request and its allow-list,
as it promised for Phase 2. Architecture describes the updater's layout
and trust chain; the build guide, the offline installer; the roadmap
marks sub-project B done."
git log --oneline origin/main..main
```
