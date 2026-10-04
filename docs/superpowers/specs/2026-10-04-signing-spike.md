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
