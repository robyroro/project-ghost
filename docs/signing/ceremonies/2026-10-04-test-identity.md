# Key ceremony: test identity, 2026-10-04

- Tool: `tools/ceremony.py` at commit `1edead4d23597a5f4634683a09e7a80582dc6a7d`
- Machine: the reference machine and its TPM

| Key | Where it lives | SHA-256 of the public key (DER SubjectPublicKeyInfo) |
|---|---|---|
| Publisher primary, `ProjectGhost-test-publisher-1` | This PC's TPM, not exportable. PIN on each use: yes | `06c5ab97148569cf478dd0466313dd34a2d30a6c0f58386fed0992e148a8b841` |
| Publisher backup | `ghost-test-publisher-backup.p8`, kept off this PC; PKCS#8 encrypted with a password stretched by PBKDF2 600,000 times | `b88bdfbfeb6b76c6ec080a21b9895aac965114472f163e11f89c8deaf2928698` |
| CUP, version 2 | `cup_key_2.json`, outside the repository, for the update server | `561e83cf7e0f21ca9ab4f72b288b5e16c311589b43913328ba9e36031d50a295` |

**Authenticode certificate:** `CN=Project Ghost Test Code Signing`, SHA-1 thumbprint `44135B4E289F81BCEB37AC90A1C6B1453459C32B`, valid three years from the ceremony. Its key is in this PC's TPM; its public half is `branding/signing/test_codesign.cer`.

**Written to the repository:** `branding/keys/test.h`, the certificate, `test/updater/data/test_identity/`, `test/updater/cup_vector_test_identity.json`, and this record.
