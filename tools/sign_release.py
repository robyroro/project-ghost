#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Signs a release of the browser: what the release pipeline calls (sub-project D).

  python tools/sign_release.py --src SRC --browser-out out\\vanilla --identity test
      --output DIR [--crx [--publisher-backup FILE]]
      [--offline-installer --updater-out out\\updater --version V --appid A]

1. Checks that the output directories build --identity, and that the
   publisher key and the Authenticode certificate are the ones it pins.
2. Signs every PE file inside the browser's mini_installer.exe, then the
   installer itself.
3. --crx: packs it into update.crx3, with the publisher proof made in the
   TPM, or by the offline backup with --publisher-backup (the recovery).
4. --offline-installer: builds the signed and tagged offline installer
   around the same mini_installer.exe.
5. Verifies every signature, the proof and the tag, then writes to DIR.
"""

from __future__ import annotations

import argparse
import getpass
import shutil
import sys
import tempfile
from pathlib import Path

import authenticode
import ceremony
import crx3
import mini_installer
import offline_installer
import repo
import signing
import update_server


def check_identity(identity: str, out_dirs: list[Path], publisher: signing.Signer | None,
                   pinned: signing.PinnedKeys) -> list[str]:
    problems = [f"{d} builds the {signing.build_identity(d)} identity, not {identity}"
                for d in out_dirs if signing.build_identity(d) != identity]
    if publisher is not None and publisher.key_hash not in pinned.publisher_hashes:
        problems.append(f"the publisher key {publisher.name} is not pinned for the "
                        f"{identity} identity")
    return problems


def argument_problem(args: argparse.Namespace) -> str | None:
    if not (args.crx or args.offline_installer):
        return "pass --crx, --offline-installer or both"
    if args.publisher_backup and not args.crx:
        return "--publisher-backup signs the CRX3; pass --crx"
    if args.offline_installer and not (args.version and args.appid and args.updater_out):
        return "--offline-installer needs --updater-out, --version and --appid"
    return None


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--src", type=Path, required=True, help="Chromium checkout (src)")
    p.add_argument("--browser-out", type=Path, required=True,
                   help="relative to --src: holds mini_installer.exe")
    p.add_argument("--updater-out", type=Path,
                   help="relative to --src: a static build with UpdaterSetup.exe")
    p.add_argument("--identity", choices=sorted(ceremony.IDENTITIES), required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--crx", action="store_true")
    p.add_argument("--publisher-backup", type=Path)
    p.add_argument("--offline-installer", action="store_true")
    p.add_argument("--version")
    p.add_argument("--appid")
    p.add_argument("--depot-tools", type=Path, default=offline_installer.DEPOT_TOOLS)
    args = p.parse_args(argv)
    problem = argument_problem(args)
    if problem:
        print(problem, file=sys.stderr)
        return 2
    if args.output.exists() and any(args.output.iterdir()):
        print(f"{args.output} is not empty", file=sys.stderr)
        return 2

    identity = ceremony.IDENTITIES[args.identity]
    src = args.src.resolve()
    browser_out = src / args.browser_out
    updater_out = src / args.updater_out if args.updater_out else None
    publisher = None
    if args.crx:
        if args.publisher_backup:
            password = getpass.getpass("Password for the backup key: ").encode()
            publisher = signing.backup_signer(args.publisher_backup, password)
        else:
            import tpm
            publisher = tpm.open_signer(identity.publisher_key)
    certificate = (repo.REPO_ROOT / "branding" / "signing"
                   / f"{identity.name}_codesign.cer").read_bytes()
    thumb = authenticode.thumbprint(certificate)
    problems = check_identity(identity.name, [d for d in (browser_out, updater_out) if d],
                              publisher, signing.pinned_keys(identity.name))
    if not authenticode.has_certificate(thumb):
        problems.append(f"the certificate {thumb} isn't in the My store with its private key")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1

    signtool = authenticode.signtool_path()
    lzma = src / "third_party" / "lzma_sdk" / "bin" / "host_platform" / "7za.exe"
    work = Path(tempfile.mkdtemp(prefix="sign-release-"))
    try:
        products = work / "products"
        products.mkdir()
        mini = products / "mini_installer.exe"
        inside = mini_installer.sign(
            browser_out / "mini_installer.exe", mini, thumb, signtool=signtool, lzma=lzma,
            makecab=src / "chrome" / "tools" / "build" / "win" / "makecab.py",
            work=work / "sign")
        print(f"signed mini_installer.exe and {len(inside)} PE files inside it", flush=True)
        failures = authenticode.verify(
            [mini, *mini_installer.unpacked_pe_files(mini, work / "verify-mini", lzma)], thumb,
            require_trusted=False)
        if args.crx:
            data = crx3.build({"mini_installer.exe": mini.read_bytes()},
                              signing.file_signer(update_server.CRX_KEY_FILE), [publisher])
            if publisher.public_der not in crx3.verified_keys(data):
                failures.append("update.crx3: the publisher proof doesn't verify")
            (products / "update.crx3").write_bytes(data)
        if args.offline_installer:
            setup = products / offline_installer.output_name()
            code = offline_installer.build(
                src, args.updater_out, mini, args.version, args.appid, setup,
                offline_installer.Signing(signtool, identity.codesign_name,
                                          authenticode.TIMESTAMP_SERVICES[0]),
                args.depot_tools)
            if code:
                raise signing.SigningError(f"offline_installer exited with {code}")
            failures += authenticode.verify(
                [setup, *mini_installer.unpacked_pe_files(setup, work / "verify-setup", lzma)],
                thumb, require_trusted=False)
            tag = offline_installer.read_tag(updater_out, setup)
            if tag != offline_installer.tag_string(args.appid):
                failures.append(f"{setup.name}: tag {tag!r}")
        if failures:
            raise signing.SigningError("verification failed:\n  " + "\n  ".join(failures))
        args.output.mkdir(parents=True, exist_ok=True)
        for product in products.iterdir():
            shutil.move(str(product), args.output / product.name)
            print(f"wrote {args.output / product.name}")
    except signing.SigningError as e:
        print(f"sign_release: {e}", file=sys.stderr)
        return 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
