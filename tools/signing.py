#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The keys that sign Ghost's packages, whatever holds them (sub-project D).

A Signer is a public key and a way to sign a SHA-256 digest with its private
half. The private half is a key file (the committed development keys), the
offline backup (password-encrypted PKCS#8), or a key in this PC's TPM
(tpm.py); callers never see which.

The identity headers in branding/keys/ pin each identity's CUP key and its
two publisher keys; this module renders and reads them.
"""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence

import ecdsa_p256
import repo

KEYS_DIR = repo.REPO_ROOT / "branding" / "keys"


class SigningError(Exception):
    pass


@dataclass(frozen=True)
class Signer:
    name: str
    public_der: bytes
    sign_digest: Callable[[bytes], bytes]  # SHA-256 digest -> DER ECDSA signature

    def sign(self, message: bytes) -> bytes:
        return self.sign_digest(hashlib.sha256(message).digest())

    @property
    def key_hash(self) -> bytes:
        return hashlib.sha256(self.public_der).digest()


def scalar_signer(name: str, d: int) -> Signer:
    return Signer(name, ecdsa_p256.spki(ecdsa_p256.public_key(d)),
                  lambda digest: ecdsa_p256.der_signature(*ecdsa_p256.sign_digest(d, digest)))


# --- Key files ---------------------------------------------------------------------

def load_key_file(path: Path) -> int:
    return int(json.loads(Path(path).read_text(encoding="utf-8"))["private_key"], 16)


def write_key_file(path: Path, d: int, comment: str) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"comment": comment, "private_key": f"{d:064x}"}, indent=2)
                    + "\n", encoding="utf-8", newline="\n")


def file_signer(path: Path) -> Signer:
    return scalar_signer(Path(path).name, load_key_file(path))


# --- The offline backup ------------------------------------------------------------
# PKCS#8 encrypted with a password: a standard format OpenSSL also reads, so
# recovering the key doesn't depend on these tools.

def write_backup(path: Path, d: int, password: bytes) -> None:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    key = ec.derive_private_key(d, ec.SECP256R1())
    Path(path).write_bytes(key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
        serialization.BestAvailableEncryption(password)))


def read_backup(path: Path, password: bytes) -> int:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    try:
        key = serialization.load_pem_private_key(Path(path).read_bytes(), password)
    except (ValueError, TypeError):
        raise SigningError(f"{Path(path).name}: wrong password, or not a backup key") from None
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        raise SigningError(f"{Path(path).name}: not a P-256 key")
    return key.private_numbers().private_value


def backup_signer(path: Path, password: bytes) -> Signer:
    return scalar_signer(Path(path).name, read_backup(path, password))


# --- Identity headers --------------------------------------------------------------

_MPL = ("// This Source Code Form is subject to the terms of the Mozilla Public\n"
        "// License, v. 2.0. If a copy of the MPL was not distributed with this\n"
        "// file, You can obtain one at https://mozilla.org/MPL/2.0/.\n")
_PROVENANCE = {
    "dev": ("Development identity: the private keys are committed in test/updater/,\n"
            "// so anyone can sign with them. Builds pin these keys unless\n"
            "// ghost_signing_identity (branding/signing.gni) says otherwise."),
    "test": ("Test identity (Phase 2): the publisher primary is in the reference\n"
             "// machine's TPM, the backup on an offline USB stick, the CUP key on the\n"
             "// update server (docs/signing/ceremonies/)."),
}


@dataclass(frozen=True)
class PinnedKeys:
    cup_version: int
    cup_der: bytes
    publisher_hashes: tuple[bytes, ...]


def _bytes_list(data: bytes, indent: str) -> str:
    lines = [", ".join(f"0x{b:02x}" for b in data[i:i + 12]) for i in range(0, len(data), 12)]
    return ",\n".join(indent + line for line in lines)


def render_identity_header(identity: str, cup_version: int, cup_der: bytes,
                           publisher_ders: Sequence[bytes], generator: str) -> str:
    if len(publisher_ders) != 2:
        raise ValueError("an identity pins two publisher keys: the primary and the backup")
    guard = f"GHOST_BRANDING_KEYS_{identity.upper()}_H_"
    hashes = "".join("    {{\n" + _bytes_list(hashlib.sha256(der).digest(), "        ")
                     + ",\n    }},\n" for der in publisher_ders)
    return (_MPL + f"\n// Generated by {generator}.\n// {_PROVENANCE[identity]}\n\n"
            f"#ifndef {guard}\n#define {guard}\n\n#include <stdint.h>\n\n#include <array>\n\n"
            "namespace ghost {\n\n"
            "// The CUP key version the client announces in cup2key.\n"
            f"inline constexpr int kCupKeyVersion = {cup_version};\n\n"
            "// DER SubjectPublicKeyInfo of the ECDSA P-256 key that signs update\n"
            "// responses (patches/0020).\n"
            "inline constexpr auto kCupPublicKey = std::to_array<uint8_t>({\n"
            + _bytes_list(cup_der, "    ") + ",\n});\n\n"
            "// SHA-256 of the DER SubjectPublicKeyInfo of each key whose proof\n"
            "// CRX3_WITH_GHOST_PUBLISHER_PROOF accepts (patches/0018): the primary,\n"
            "// then the backup.\n"
            "inline constexpr std::array<std::array<uint8_t, 32>, 2>\n"
            "    kCrxPublisherKeyHashes = {{\n"
            + hashes + "}};\n\n"
            "}  // namespace ghost\n\n"
            f"#endif  // {guard}\n")


def _hex_bytes(text: str) -> bytes:
    return bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", text))


def pinned_keys(identity: str, keys_dir: Path = KEYS_DIR) -> PinnedKeys:
    text = (keys_dir / f"{identity}.h").read_text(encoding="utf-8")
    version = re.search(r"kCupKeyVersion = (\d+);", text)
    cup = re.search(r"kCupPublicKey = std::to_array<uint8_t>\(\{(.*?)\}\);", text, re.S)
    hashes = re.search(r"kCrxPublisherKeyHashes = \{\{\n(.*?)\}\};", text, re.S)
    if not (version and cup and hashes):
        raise SigningError(f"{identity}.h is not an identity header")
    return PinnedKeys(int(version.group(1)), _hex_bytes(cup.group(1)), tuple(
        _hex_bytes(block) for block in re.findall(r"\{\{(.*?)\}\}", hashes.group(1), re.S)))


def build_identity(out_dir: Path) -> str:
    """The identity an output directory's args.gn builds; "dev" when unset."""
    text = (Path(out_dir) / "args.gn").read_text(encoding="utf-8")
    match = re.search(r'^\s*ghost_signing_identity\s*=\s*"(\w+)"', text, re.M)
    return match.group(1) if match else "dev"
