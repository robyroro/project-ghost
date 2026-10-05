# Signing: progress notes

- Phase 2, sub-project D ([design](2026-10-04-signing-design.md), [plan](../plans/2026-10-04-signing.md))
- Machine: the reference machine (Ryzen 5 3600, 32 GB), AMD firmware TPM 2.0

## Spike, 2026-10-04

| Question | Answer |
|---|---|
| ECDSA P-256 key in the TPM (CNG, Microsoft Platform Crypto Provider) | Created; export policy `None`, so not exportable |
| 20 TPM signatures (ECDSA P-256) | 315 ms |
| A PIN on each use (`ProtectKey` UI policy) | Asked when the key is created, to set it. Then asked each time a process opens the key to sign: four signatures from two other processes waited 5–24 s each, the time to type it. Two signatures through the same open handle, in the creating process, asked nothing more. |
| ECDSA code-signing certificate in the TPM (`New-SelfSignedCertificate`) | **Fails**: `NTE_PROV_TYPE_NOT_DEF` (0x80090017), also with `-KeySpec None`. The same cmdlet works with ECDSA in the software provider, and with RSA 2048 in the TPM. CertEnroll can't put an ECDSA key in this provider; CNG itself can (above). |
| RSA 2048 code-signing certificate in the TPM | Works; the certificate has its private key |
| Timestamp services | digicert: exit 0, 638 ms for one file; sectigo: exit 0, 612 ms (signature included) |
| Signing speed, RSA 2048 in the TPM | 10 files in one `signtool` call: 764 ms without timestamps, 2,694 ms with digicert's |
| The host's `signtool verify /pa` | "A certificate chain processed, but terminated in a root certificate which is not trusted by the trust provider." `Get-AuthenticodeSignature` reports `UnknownError` |
| The Sandbox, certificate in `Root` | `spike.exe` `Valid`; `resigned.exe`, signed twice, `Valid` |
| Anything pinning the Authenticode certificate | Nothing. `WinVerifyTrust`, `CryptQueryObject`, `CertGetCertificateChain`, signer names: no match in `chrome/updater`, `chrome/installer`, `chrome/install_static`, `chrome/chrome_elf`, `components/update_client` or `components/crx_file` outside tests. `certificate_tag.cc` only writes tags. |
| PE files in a component build's `mini_installer` | 183 DLLs beside `setup.exe` (BD resources), 735 in `chrome.7z`, and `setup.exe`: 919 |

**A tagged metainstaller run with only `--silent`** reads its tag: `chrome/updater/win/installer/installer.cc` appends `--install=<tag>` when the command line has none. The end-to-end test confirms it.

**Decisions:**
- The test identity's Authenticode certificate is **RSA 2048** in the TPM (`CODESIGN_KEY_ARGS` in `tools/ceremony.py`). The publisher keys stay ECDSA P-256: `tools/tpm.py` creates them through NCrypt, which works. A bought certificate will have its own key and algorithm, so nothing here carries over to production.
- The publisher primary gets a **PIN** (`ceremony.py --pin`). `tools/tpm.py` opens the key for each signature, so each publisher proof asks for it: once per release.
- The host verifies signatures with `WinVerifyTrust`'s code, not `Get-AuthenticodeSignature`'s status: an untrusted root and other failures both show as `UnknownError` there.
- Signing a component build's 919 files with timestamps takes about four minutes (270 ms a file).

## Builds

All with `autoninja -j 10` on the reference machine.

| Build | Time |
|---|---|
| `out/vanilla` after switching to the test identity (Task 13: `ghost_unittests`, `mini_installer`) | 1.8 min |
| `out/updater` after switching to the test identity (three updater targets) | 0.6 min |
| Respin `-1`, `-2`, `-3` (`mini_installer`) | 7.3, 7.4, 7.4 min |
| `out/updater` with patch 0018 mutated (Task 17, M4) | 2.2 min |
| `out/vanilla` back to the development identity (Task 19: `ghost_unittests`) | 7.3 min |
| `out/updater` back to the development identity | 0.5 min |

Patch 0018's first build (Task 7) was not timed.

## The end-to-end run

**Passed on 2026-10-04 in Windows Sandbox**, under the test identity, in about 7 minutes:

| Input | Made by | Time |
|---|---|---|
| Respin `-1`: signed `mini_installer.exe` and the signed, tagged `ProjectGhostOfflineSetup.exe` | `sign_release.py --offline-installer` | 16.2 min |
| Respin `-2`: `update.crx3`, publisher proof by the primary in the TPM (PIN asked once) | `sign_release.py --crx` | 15.3 min |
| Respin `-3`: `update.crx3`, publisher proof by the backup (password asked once) | `sign_release.py --crx --publisher-backup` | not timed |

Each run of `sign_release.py` signed `mini_installer.exe` and the 919 PE files inside it. About 10 of its 15–16 minutes go to repacking `chrome.7z` with LZMA at the ultra setting, as upstream's packer does.

Every step passed: clean machine, trust the signing certificate, install (`--silent` only, the tag naming the app), signatures (every PE file under the browser's and the updater's directories), update to `…14902`, recovery update (backup publisher key) to `…14903`, signatures after the update, launch, privacy, uninstall.

## Mutation checks

Run 2026-10-04 and 2026-10-05. Each input was built outside the repository, the test run in a fresh Sandbox with the other inputs unchanged, and the change undone.

| Change | Failed |
|---|---|
| M1: the update's CRX3 proved by a third key (`update_server.OTHER_KEY`) | **update:** `pv stayed '152.0.7977.14901'` and no `…14902` directory appeared; the updater exited with 0 and installed nothing. |
| M2: `chrome.dll` left unsigned inside a signed `mini_installer` | `sign_release.py`'s verification reports `chrome.dll: signed by nobody`, so it would refuse. Installed anyway: **signatures** fails with the same message. |
| M3: an offline installer signed but not tagged | **install:** the metainstaller exited with 75009 (`kErrorUnknownCommandLine`). With no tag it appends no `--install=`, so the updater gets only `--silent`. |
| M4: patch 0018 accepting only the first pinned key (`key_hash == ghost::kCrxPublisherKeyHashes[0]`) | **update** passed, then **recovery update (backup publisher key)** failed: the updater logs `Verifying component` for the backup-proved package and never `Verification successful`, and `pv` stays `…14902`. |

After M4, `crx_verifier.cc` was restored from git and `out/updater` rebuilt. The M4 run left no `result.json` in its results directory, only `run.log` and `updater.log`. The step results were in `run.log`.

## Findings

- **No USB stick was at hand**, so the test identity's publisher backup is in the user's cloud storage. The `cryptography` package encrypts PKCS#8 with only 2048 PBKDF2 rounds, so `signing.py` writes the PBES2 structure itself with 600,000. OpenSSL and `cryptography` both read it (commit 1edead4).
- **`offline_installer.build` didn't create its output directory.** M4 found it (commit 940f7fb).
- **`mini_installer.py` called `ctypes.WINFUNCTYPE` at import**, which exists only on Windows, so the module didn't import elsewhere. Its callback types are now made where they're used (commit 61a6f1b).
- **`release_version.py write` refuses a `chrome\VERSION` it already wrote.** Building two respins in a row needs `git checkout -- chrome\VERSION` between them; plan Task 19 Step 2 left it out, and its second build was the first respin again until it was re-run.

## The development identity after D

Run 2026-10-05, with `ghost_signing_identity` removed from both `args.gn` files:
- `ghost_unittests`: 26/26 (the test-identity-only test isn't compiled).
- **The unsigned end-to-end test passed in Windows Sandbox** in about 7.5 minutes: clean machine, install (install arguments on the command line), update `…14901 → …14902` (a CRX3 proved by the committed development key), launch, privacy, uninstall. Respin `-1` took 8.4 min, respin `-2` 10 min.
