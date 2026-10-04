# Signing: design

- Status: design approved 2026-10-04
- Phase 2, sub-project D ([roadmap](../../roadmap.md#phase-2-release-engineering))
- Depends on sub-projects B ([branded updater](2026-10-02-branded-updater-design.md)) and C ([update server](2026-10-03-update-server-design.md))

## Goal

Every file Ghost ships is signed, and the keys that sign them are held the way production keys will be. A release is Authenticode-signed throughout, its offline installer is tagged, and its update package carries a publisher proof made by a key that never exists as a file. If that key is lost, a backup key held offline signs the next release and the installed browsers accept it. All of it runs end to end in Windows Sandbox under Phase 2's test identity.

## Decisions

These were settled in discussion on 2026-10-04:

- **The full mechanism, with test-identity keys held as production keys will be.** The keys D creates belong to the test identity: only test machines trust them. They are generated and kept exactly where production keys will be, so the custody procedure is proven. When the final name is chosen, the same ceremony makes the production keys. Production keys made now would precede any public build and any VPS, and a change of design in E could make them obsolete.
- **No spending until the product justifies it.** Everything in D is free. Bought hardware and a bought certificate come at the end, and each is a change of backend or certificate, not of design.
- **The offline keys live in this PC's TPM.** Windows 11 requires TPM 2.0; the reference machine has AMD's firmware TPM 2.0, initialized and ready for storage (`tpmtool getdeviceinformation`, 2026-10-04).
  - A key generated in the TPM through the Microsoft Platform Crypto Provider can't be exported, so it never exists as a file on disk. It satisfies [threat-model.md](../../threat-model.md#update-security)'s "hardware-backed", at no cost.
  - **Its limits:** the key is bound to this PC. Clearing the TPM, a firmware TPM reset or a dead motherboard loses it, which is why a backup publisher key is pinned. Malware running on the PC can use the key without anyone's presence; a YubiKey's touch requirement would prevent that. A PIN on each use narrows it, if the TPM provider supports one (the spike checks).
  - Rejected: a YubiKey pair (about €55–60 each, the preferred upgrade later), a cloud KMS (a credential on the builder could sign without anyone present, plus an account and billing), and password-encrypted files as the primary custody (contradicts the threat model).
- **Every PE file Ghost ships is Authenticode-signed**, as Chrome does: `chrome.exe`, `chrome.dll`, `chrome_elf.dll`, `setup.exe`, `updater.exe`, the helpers, then the outer installers.
  - Windows 11's Smart App Control blocks unsigned binaries without reputation, so a signed installer that installs an unsigned `chrome.exe` isn't enough. Enterprise AppLocker and WDAC rules also work by signature.
  - With the test certificate, nothing passes Smart App Control on a real machine: only the Sandbox trusts the certificate. The signing chain is the one production uses; at the end only the certificate changes.
- **Two publisher keys are pinned, a primary and a backup.** Losing the only publisher key would strand every installed browser: no update would ever verify again, and every user would have to reinstall by hand. The backup lives in a different place from the primary and is never used in normal operation.
- **The CUP key is the online key.** The server signs each response live, over the client's nonce, so the CUP key has to be on the server ([C's design](2026-10-03-update-server-design.md#the-vps)). The publisher key is the offline one. A compromised server can withhold updates but not serve a malicious one, because it can't make a publisher proof. The threat model's "our update servers never hold signing keys" is corrected to say this.
- **CUP stays ECDSA P-256.** Upstream's post-quantum ML-DSA-44 key is chosen only with the `PqcCupSigning` feature, off by default at 152. It is reconsidered when upstream turns it on.
- **CUP key versions are global:** 1 is the development key (committed), 2 the test identity's custody key, 3 the production key at the final name.
- **Components stay out.** Switching the component updater to Ghost's publisher proof ([B's spike](2026-10-02-branded-updater-spike.md)) waits for Ghost's first component.

## The keys

| Key | Algorithm | Where it lives | Used for |
|---|---|---|---|
| Publisher primary, `ProjectGhost-test-publisher-1` | ECDSA P-256 | This PC's TPM, not exportable; a PIN on each use if the spike confirms it | The publisher proof of every update package, once per release |
| Publisher backup | ECDSA P-256 | Off this PC: for the test identity, the user's cloud storage (no USB stick on hand, decided 2026-10-04). A PKCS#8 file encrypted with the user's password, stretched by PBKDF2-HMAC-SHA256 600,000 times, so a stolen copy makes each guess expensive | The publisher proof only when the primary is lost or compromised |
| Authenticode | RSA 2048, in a self-signed code-signing certificate (CertEnroll can't put an ECDSA key in the TPM: [spike](2026-10-04-signing-spike.md)) | This PC's TPM, the certificate in the user's `My` store; no PIN, since a release signs hundreds of files | Every PE file and installer |
| CUP, version 2 | ECDSA P-256 | A file outside the repository, deployed to the server through `LoadCredential=` (C) | Signing update responses live |

**The development keys stay committed** in `test/updater/`, marked test-only, so contributors, CI and the local end-to-end test need no TPM. D adds a committed development backup publisher key, so the backup path is tested everywhere.

**Which set a build pins** is a new GN argument, `ghost_signing_identity`: `"dev"` (the default) or `"test"`. It works like `ghost_update_url`. Both sets' public halves are committed in `branding/`; a buildflag selects one at compile time.

## Components

### In `//ghost`

| Unit | What it does |
|---|---|
| `tools/signing.py` | A `Signer` per key: a public key and `sign_digest(sha256) -> DER ECDSA signature`. Signers from a key file (the committed development keys, through `tools/ecdsa_p256.py`) and from the offline backup; renders and reads the identity headers. Uses the `cryptography` package for the backup's encrypted PKCS#8, and only there; the package is pinned in `tools/requirements.txt` and installed in CI. A YubiKey signer can be added later without changing callers. |
| `tools/tpm.py` | Keys in the TPM: NCrypt through `ctypes` on the Microsoft Platform Crypto Provider. Creates, opens as a `Signer`, deletes. Standard library only. |
| `tools/ceremony.py` | Creates an identity's keys (below), writes their public halves to `branding/` and a ceremony record to `docs/signing/ceremonies/`. |
| `tools/authenticode.py` | Signs with `signtool`, the timestamp fallback; verifies with `WinVerifyTrust` and the signer's thumbprint. |
| `tools/mini_installer.py` | Unpacks the browser installer's resources, signs every PE file in them, packs them again with upstream's formats and `UpdateResource`, signs the installer. Never writes to `out/`. Lists the PE files inside any installer, for verification. |
| `tools/sign_release.py` | The steps of [Signing a release](#signing-a-release), in order. |
| `tools/crx3.py` | `build` takes a developer `Signer` and publisher `Signer`s instead of a key. The developer proof, which only derives the CRX ID and confers no trust, uses a fixed key: the committed development publisher key. The package's CRX ID therefore stays the same when the publisher key changes, as in the recovery drill. |
| `tools/offline_installer.py` | Runs upstream's `sign.py` with `--identity` instead of `--disable_tag_and_sign`, then writes the tag with `tag.exe --set-tag=…`. The installer no longer needs B's install arguments on its command line. |
| `tools/update_server.py` | Takes the CUP key file and its version as arguments, so the local end-to-end test can run with either identity. `keygen` also creates the development backup publisher key. |
| `tools/lint.py` | Fails on private-key material outside `test/updater/`: a PEM `PRIVATE KEY` block, or a JSON `private_key` field. |
| `branding/` | Each identity's keys in its own generated header. `cup_key.h` and `crx_publisher_key.h` select one by buildflag. `crx_publisher_key.h` pins an array of two hashes. |
| `branding/BUILD.gn`, `branding/updater.gni` | The `ghost_signing_identity` argument and its buildflag. |
| Patch 0018 | `CRX3_WITH_GHOST_PUBLISHER_PROOF` accepts a proof by any pinned hash. |
| `updater/crx_verifier_unittest.cc` | Accepts the primary and the backup, rejects a third key. Its fixtures follow the build's identity: the test identity's are signed once by the ceremony keys and committed (they hold public data only). |
| `tools/update_smoke.py` | Imports the test certificate in the Sandbox, runs the installer without arguments, checks every installed PE signature, and runs the recovery drill (below). |
| `docs/signing/` | How releases are signed, the recovery and rotation runbook, and the ceremony records. |

### In `project-ghost-update-server`

- The service loads several CUP keys, each with its version, and signs with the version the client names in `cup2key`. A request naming an unknown version is refused (400) and logged without client data.
- `LoadCredential=cup_keys:/etc/ghost-update/cup_keys` loads one file per key version, as the credentials `cup_keys_<version>.json`; the service refuses to start without at least one.
- Tests cover two versions side by side and the unknown version.

## The ceremony

`ceremony.py init --identity test`, run once on the reference machine by the user:

1. **The publisher primary** is created in the TPM as `ProjectGhost-test-publisher-1`, not exportable, with a PIN if the spike confirms it works.
2. **The Authenticode key and certificate** are created in the TPM with `New-SelfSignedCertificate -Type CodeSigningCert -Provider "Microsoft Platform Crypto Provider"`, RSA 2048. The certificate's public half is exported to `branding/signing/test_codesign.cer` for the Sandbox.
3. **The publisher backup** is generated in memory and written as PKCS#8 encrypted with the user's password (PBES2: PBKDF2-HMAC-SHA256, 600,000 rounds, then AES-256-CBC): a standard format OpenSSL also reads, so recovering it doesn't depend on our tools. The `cryptography` package's own PKCS#8 encryption stretches a password only 2048 times, so `tools/signing.py` writes the structure itself. The ceremony reads the file back, decrypts it, compares the public key, then drops the key from memory. The user moves the file off this PC.
4. **The CUP key, version 2,** is written to a file outside the repository, for `tools/deploy.py`.
5. **The public halves** go to `branding/`, and the record to `docs/signing/ceremonies/<date>-test-identity.md`, named by the day the ceremony runs: each key's fingerprint, where it lives, the date, and the tool's commit.

The ceremony refuses to overwrite an existing key of the same name. Running it again for the same identity is a rotation, and goes through the runbook.

## Signing a release

What E will call. In D it is run by hand.

1. **The build**, unchanged: `out/vanilla` and `out/updater` with `ghost_signing_identity = "test"`.
2. **Before signing anything,** the tool reads the identity from `args.gn` and checks that every signing key's public half equals the one pinned for it in `branding/`. A mismatch stops the run. This prevents signing a development build with custody keys, or the reverse.
3. **`mini_installer.py`, through `authenticode.py`:**
   - unpacks `mini_installer.exe`'s resources to a work directory: `chrome.packed.7z` and the `chrome.7z` inside it, `setup.ex_`, and a component build's DLLs;
   - signs each PE file with `signtool`, the certificate selected from the `My` store, SHA-256, with an RFC 3161 timestamp from a free public timestamp service; a second service is tried if the first fails;
   - packs them again with `create_installer_archive.py`'s formats and writes them into a copy of `mini_installer.exe` with `UpdateResource`, as `sign.py` does for the metainstaller;
   - signs that `mini_installer.exe`.
4. **`crx3.py`** packs the signed `mini_installer.exe` into a CRX3 with the publisher proof from the TPM. The order matters: the update package carries signed binaries.
5. **`offline_installer.py`** builds the metainstaller around the same signed `mini_installer.exe`; `sign.py` signs `updater.exe` and the metainstaller, then the tag is written.
6. **The final check:** `signtool verify /pa` on every PE file, including those inside the archives, and the CRX3's publisher proof against the pinned hashes. Any failure stops the release.
7. **`release.py`** (C) publishes the CRX3 to the server.

Everything is written to a temporary directory and moved to the output only when every step has passed, so no half-signed artifact is left behind.

## Recovery and rotation

The runbook in `docs/signing/` covers:

- **The publisher primary is lost** (TPM cleared, PC dead): the next release is signed with the backup, which the clients already accept. That release pins a new pair, a new primary and a new backup, and drops the lost key.
- **The publisher primary is compromised:** the same, urgently. Serving an update also takes control of the server, for CUP. Updated clients no longer accept the old key.
- **CUP rotation:** the server gains version N+1; a release ships its public key; the server signs with the version each client asks for. The old version stays on the server until the support window E sets.
- **The Authenticode certificate:** expiry is covered by the timestamps, and rotation is a new certificate. The spike checks that nothing in the client pins it.
- **The backup is used:** a new backup is made at once, by the ceremony's backup step, because the old one has left its offline place.

## Error handling

| Case | Behavior |
|---|---|
| A signing key's public half differs from the pinned one | Nothing is signed; the run stops naming the key. |
| TPM missing, locked out, or the key absent | The run stops with the TPM's error. It never falls back to another key. |
| Wrong PIN, or wrong backup password | Refused; nothing about the key is printed. |
| Both timestamp services fail | The run stops; nothing reaches the output. |
| `signtool verify` or the CRX3 check fails | The release stops. |
| The ceremony finds a key with the same name | It refuses; rotation goes through the runbook. |

## Testing

- **Unit tests, in CI on Ubuntu and Windows:** the signers; `crx3.py` with signers; the ceremony with an in-memory fake custody (headers and record written correctly, overwrite refused); the backup's PKCS#8 written and read back; the identity check before signing; the resource round trip and the signing order on a small PE fixture (Windows); the lint's private-key check.
- **On the real TPM, locally only:** `tpm.py` creates a temporary key, signs, verifies with `tools/ecdsa_p256.py`, and deletes the key. Skipped in CI.
- **C++:** `crx_verifier_unittest` accepts the primary and the backup and rejects a third key, in both identities.
- **The end-to-end test in Windows Sandbox,** with the test identity:
  - **Inputs:** `ghost_signing_identity = "test"` set in `out/vanilla` and `out/updater`, an incremental rebuild of the targets that include the keys. No new output directory, and none renamed.
  - The Sandbox imports `test_codesign.cer` into `Root` and `TrustedPublisher`.
  - The offline installer for respin `-1` runs **with no arguments**: the tag supplies them.
  - Every installed PE file has a `Valid` Authenticode signature.
  - The update `-1 → -2`, its publisher proof made in the TPM.
  - **The recovery drill:** the update `-2 → -3`, its publisher proof made with the backup key. The client must accept it.
  - Uninstall, and the updater's removal, as in B.
- **Mutation checks**, each of which must fail the test:
  - a CRX3 whose publisher proof is made by a third key;
  - an unsigned `chrome.dll` in the package;
  - an untagged metainstaller;
  - patch 0018 accepting only the first pinned hash (the recovery drill must fail).

## For the spike

Questions the implementation answers first, recorded in the progress notes:

- Does the Microsoft Platform Crypto Provider enforce a PIN on each use of an ECDSA key, through NCrypt's UI policy? If not, the publisher key goes without, and the record says so.
- Do `signtool` and `WinVerifyTrust` in the Sandbox accept a certificate whose key is in the TPM? (RSA 2048: yes; ECDSA can't be made there by `New-SelfSignedCertificate`.)
- Does anything in the browser, the installer or the updater pin the Authenticode certificate?
- `sign.py` signs every signable file inside the metainstaller's archive, including the already-signed `mini_installer.exe`. Re-signing with the same certificate must leave a valid single signature.
- How long does signing a component build's files take? `out/vanilla` is a component build, so it has far more DLLs than a release build will.
- Which free timestamp services answer reliably, and what each request sends: only a hash.

## Done when

1. The ceremony has run; its record is committed, and the backup is verified and stored off this PC.
2. The end-to-end test passes, recovery drill included, and the mutation checks fail as required.
3. The update server's repository signs with versioned CUP keys, with its tests passing in CI. Its deployment stays deferred, as decided for C.
4. The documentation is updated:
   - [threat-model.md](../../threat-model.md#update-security): the CUP key is the online key, the publisher key the offline one, with a backup;
   - [architecture.md](../../architecture.md#updates-and-signed-data): signing as built;
   - `docs/signing/`: the release signing steps, the runbook, the ceremony records;
   - the build guide: `ghost_signing_identity`;
   - the roadmap marks D done.

## Out of scope

- **Buying anything:** a YubiKey pair, a code-signing certificate. At the end, when the product justifies it. A free option to check then is SignPath Foundation's program for open-source projects, whose build requirements must be read first.
- **The production keys:** the same ceremony, at the final name.
- **The automated release pipeline,** with SBOM and provenance: sub-project E, which calls these tools.
- **Components** and their publisher proof: with Ghost's first component.
- **System-level installs, macOS and Linux.**
