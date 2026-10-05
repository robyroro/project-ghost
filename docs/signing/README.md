# Signing

How Ghost's releases are signed, where the keys live, and what to do when one is lost. Design: [signing design](../superpowers/specs/2026-10-04-signing-design.md).

## The keys

| Key | Signs | Where it lives |
|---|---|---|
| Publisher primary | The publisher proof of every update package (CRX3) | A TPM, not exportable |
| Publisher backup | The publisher proof, only when the primary is lost or compromised | Off the signing machine (for the test identity, the user's cloud storage), as PKCS#8 encrypted with a password stretched 600,000 times |
| Authenticode | Every PE file and installer | A TPM; the certificate in the signer's `My` store |
| CUP | Each update response, live | The update server, one credential file per key version |

The updater accepts a package signed by either publisher key (`branding/keys/<identity>.h`). The update server holds only the CUP key: a compromised server can withhold updates but can't make a publisher proof.

## Identities

`ghost_signing_identity` (`branding/signing.gni`) picks the keys a build pins:

- `dev`, the default: the keys committed in `test/updater/`. Anyone can sign with them; only development builds trust them.
- `test`: the test identity's keys, made by the [ceremony of 2026-10-04](ceremonies/2026-10-04-test-identity.md). Only test machines trust its certificate.

The production identity is made by the same ceremony when the final name is chosen.

## Signing a release

On the machine that holds the keys, after building `mini_installer` with the identity:

```
python tools\sign_release.py --src <src> --browser-out out\vanilla --identity test --output <dir> --crx
python tools\sign_release.py --src <src> --browser-out out\vanilla --identity test --output <dir> --offline-installer --updater-out out\updater --version <release version> --appid <browser app ID>
```

It refuses to sign when an output directory builds another identity, or when the publisher key or the certificate isn't the one pinned for it. It signs every PE file inside `mini_installer.exe`, then the installer; packs the CRX3; builds the signed and tagged offline installer; and verifies every signature, the proof and the tag before writing anything.

## The ceremony

`python tools\ceremony.py init --identity <identity> --backup <file>.p8 [--pin]`, run by the user on a clean, committed tree. It creates the publisher primary and the Authenticode key in the TPM, the backup as a password-encrypted file (read back to check it; the user then moves it off this PC), and the CUP key as a file for the server; it writes the identity's header, the certificate, the fixtures and a record to `ceremonies/`. It refuses to overwrite any existing key.

## Runbook

**The publisher primary is lost** (TPM cleared, PC dead):
1. Sign the next release's CRX3 with the backup: `sign_release.py … --crx --publisher-backup <file>.p8`.
2. That same release pins a new pair: run a rotation ceremony (below), then rebuild with the new header.
3. Make a new backup at once: the old one has left its offline place.

**The publisher primary is compromised:** the same, urgently. Serving an update also takes control of the update server (CUP). Updated clients no longer accept the old key.

**Rotating the publisher keys:** a new identity header with the new pair, released while the old pair still verifies; the release after it drops the old pair. `ceremony.py` refuses an existing key name, so a rotation uses the next name (`…-publisher-2`), added to `IDENTITIES`.

**Rotating the CUP key:** add version N+1 on the server beside N (`deploy.py --cup-key N=… --cup-key N+1=…`); ship a release whose header has version N+1; keep N on the server until the support window (sub-project E) ends.

**The Authenticode certificate:** signatures carry RFC 3161 timestamps, so they stay valid after it expires. Nothing in the client pins it ([spike notes](../superpowers/specs/2026-10-04-signing-spike.md)); a new certificate needs no client change.
