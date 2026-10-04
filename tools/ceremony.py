#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The key ceremony: creates a signing identity's keys (sub-project D).

    python tools/ceremony.py init --identity test --backup E:\\ghost-test-publisher-backup.p8
        [--cup-out FILE] [--pin]

In order: the publisher primary, in this PC's TPM; the Authenticode key, in
the TPM, with its self-signed certificate in the My store; the publisher
backup, written to the USB stick as password-encrypted PKCS#8 and read back;
the CUP key, as a file outside the repository for the update server. Then
the identity's header in branding/keys/, the certificate, the test fixtures
the new keys sign, and the ceremony record. It refuses to run if any of
these already exists: running it again is a rotation (docs/signing/).
"""

from __future__ import annotations

import argparse
import base64
import datetime
import getpass
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

import authenticode
import ecdsa_p256
import repo
import signing
import update_server

# The Authenticode key: RSA 2048, since CertEnroll can't put an ECDSA key in the
# TPM (docs/superpowers/specs/2026-10-04-signing-spike.md).
CODESIGN_KEY_ARGS = "-KeyAlgorithm RSA -KeyLength 2048"


@dataclass(frozen=True)
class Identity:
    name: str
    cup_version: int
    publisher_key: str   # the TPM key's name
    codesign_name: str   # the certificate's CN

    @property
    def codesign_subject(self) -> str:
        return f"CN={self.codesign_name}"


IDENTITIES = {
    "test": Identity("test", 2, "ProjectGhost-test-publisher-1",
                     "Project Ghost Test Code Signing"),
}


class CeremonyError(Exception):
    pass


class Custody(Protocol):
    def publisher_exists(self, name: str) -> bool: ...
    def create_publisher(self, name: str, pin: bool) -> signing.Signer: ...
    def codesign_exists(self, subject: str) -> bool: ...
    def create_codesign(self, subject: str) -> bytes: ...


class WindowsCustody:
    """The TPM, and the user's My store."""

    def publisher_exists(self, name: str) -> bool:
        import tpm
        return tpm.key_exists(name)

    def create_publisher(self, name: str, pin: bool) -> signing.Signer:
        import tpm
        tpm.create_key(name, pin)
        return tpm.open_signer(name)

    def codesign_exists(self, subject: str) -> bool:
        return authenticode._powershell(
            "@(Get-ChildItem Cert:\\CurrentUser\\My | Where-Object Subject -eq "
            f"'{subject}').Count").strip() != "0"

    def create_codesign(self, subject: str) -> bytes:
        text = authenticode._powershell(
            f"$c = New-SelfSignedCertificate -Type CodeSigningCert -Subject '{subject}' "
            f"{CODESIGN_KEY_ARGS} -KeyExportPolicy NonExportable "
            "-Provider 'Microsoft Platform Crypto Provider' "
            "-CertStoreLocation Cert:\\CurrentUser\\My -HashAlgorithm SHA256 "
            "-NotAfter (Get-Date).AddYears(3); [Convert]::ToBase64String($c.RawData)")
        return base64.b64decode(text.strip())


def outputs(identity: Identity, root: Path) -> dict[str, Path]:
    return {
        "header": root / "branding" / "keys" / f"{identity.name}.h",
        "certificate": root / "branding" / "signing" / f"{identity.name}_codesign.cer",
        "fixtures": root / "test" / "updater" / "data" / f"{identity.name}_identity",
        "vector": root / "test" / "updater" / f"cup_vector_{identity.name}_identity.json",
        "records": root / "docs" / "signing" / "ceremonies",
    }


_RECORD = """# Key ceremony: {name} identity, {date}

- Tool: `tools/ceremony.py` at commit `{commit}`
- Machine: the reference machine and its TPM

| Key | Where it lives | SHA-256 of the public key (DER SubjectPublicKeyInfo) |
|---|---|---|
| Publisher primary, `{publisher_key}` | This PC's TPM, not exportable. PIN on each use: {pin} | `{primary}` |
| Publisher backup | `{backup_file}` on an offline USB stick, PKCS#8 encrypted with a password | `{backup}` |
| CUP, version {cup_version} | `{cup_file}`, outside the repository, for the update server | `{cup}` |

**Authenticode certificate:** `{subject}`, SHA-1 thumbprint `{thumbprint}`, valid three years from the ceremony. Its key is in this PC's TPM; its public half is `branding/signing/{name}_codesign.cer`.

**Written to the repository:** `branding/keys/{name}.h`, the certificate, `test/updater/data/{name}_identity/`, `test/updater/cup_vector_{name}_identity.json`, and this record.
"""


def run(identity: Identity, custody: Custody, backup: Path, password: bytes, cup_out: Path,
        pin: bool, root: Path, today: datetime.date, commit: str) -> Path:
    paths = outputs(identity, root)
    existing = [str(p) for p in (paths["header"], paths["certificate"], backup, cup_out)
                if p.exists()]
    if custody.publisher_exists(identity.publisher_key):
        existing.append(f"the TPM key {identity.publisher_key}")
    if custody.codesign_exists(identity.codesign_subject):
        existing.append(f"the certificate {identity.codesign_subject}")
    if existing:
        raise CeremonyError("refusing to overwrite " + ", ".join(existing)
                            + "; a rotation follows docs/signing/README.md")

    primary = custody.create_publisher(identity.publisher_key, pin)
    certificate = custody.create_codesign(identity.codesign_subject)

    backup_key = ecdsa_p256.generate_private_key()
    expected = signing.scalar_signer("backup", backup_key).public_der
    signing.write_backup(backup, backup_key, password)
    del backup_key
    backup_signer = signing.backup_signer(backup, password)  # read back from the stick
    if backup_signer.public_der != expected:
        raise CeremonyError(f"{backup} doesn't read back as the key just written")

    cup_key = ecdsa_p256.generate_private_key()
    signing.write_key_file(cup_out, cup_key, f"The {identity.name} identity's CUP key, "
                           f"version {identity.cup_version}. For the update server only.")
    cup = signing.file_signer(cup_out)

    paths["header"].parent.mkdir(parents=True, exist_ok=True)
    paths["header"].write_text(signing.render_identity_header(
        identity.name, identity.cup_version, cup.public_der,
        [primary.public_der, backup_signer.public_der], "tools/ceremony.py"),
        encoding="utf-8", newline="\n")
    paths["certificate"].parent.mkdir(parents=True, exist_ok=True)
    paths["certificate"].write_bytes(certificate)
    update_server.write_crx_fixtures(paths["fixtures"],
                                     signing.file_signer(update_server.CRX_KEY_FILE),
                                     primary, backup_signer)
    update_server.write_vector(cup_key, identity.cup_version, paths["vector"],
                               "tools/ceremony.py")

    paths["records"].mkdir(parents=True, exist_ok=True)
    record = paths["records"] / f"{today.isoformat()}-{identity.name}-identity.md"
    record.write_text(_RECORD.format(
        name=identity.name, date=today.isoformat(), commit=commit,
        publisher_key=identity.publisher_key, pin="yes" if pin else "no",
        primary=primary.key_hash.hex(), backup_file=backup.name,
        backup=backup_signer.key_hash.hex(), cup_version=identity.cup_version,
        cup_file=cup_out.name, cup=cup.key_hash.hex(), subject=identity.codesign_subject,
        thumbprint=authenticode.thumbprint(certificate)), encoding="utf-8", newline="\n")
    return record


def _git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(repo.REPO_ROOT), *args], capture_output=True,
                          text=True, check=True).stdout.strip()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    init = sub.add_parser("init")
    init.add_argument("--identity", choices=sorted(IDENTITIES), required=True)
    init.add_argument("--backup", type=Path, required=True,
                      help="the backup key's file, on the USB stick")
    init.add_argument("--cup-out", type=Path, help="the CUP key's file, outside the repository")
    init.add_argument("--pin", action="store_true", help="a PIN on each use of the TPM key")
    args = parser.parse_args(argv)
    identity = IDENTITIES[args.identity]
    cup_out = args.cup_out or (Path.home() / "ProjectGhostKeys" / identity.name
                               / f"cup_key_{identity.cup_version}.json")
    if _git("status", "--porcelain"):
        print("commit first: the record names the tool's commit", file=sys.stderr)
        return 2
    password = getpass.getpass("Password for the backup key: ").encode()
    if len(password) < 12:
        print("the password needs at least 12 characters", file=sys.stderr)
        return 2
    if getpass.getpass("Again: ").encode() != password:
        print("the passwords differ", file=sys.stderr)
        return 2
    try:
        record = run(identity, WindowsCustody(), args.backup, password, cup_out, args.pin,
                     repo.REPO_ROOT, datetime.date.today(), _git("rev-parse", "HEAD"))
    except (CeremonyError, signing.SigningError) as e:
        print(f"ceremony: {e}", file=sys.stderr)
        return 1
    print(f"done; the record is {record.relative_to(repo.REPO_ROOT)}")
    print(f"the CUP key is {cup_out}; keep the stick offline, away from this PC")
    return 0


if __name__ == "__main__":
    sys.exit(main())
