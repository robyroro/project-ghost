# Threat model

This document says what Ghost protects, from whom, how, and where protection ends. It is reviewed at every phase exit and whenever a new trust boundary is added. Privacy guarantees are in [privacy-model.md](privacy-model.md). Mechanisms are in [architecture.md](architecture.md).

## Assets

| Asset | Why it matters |
|---|---|
| Browsing data: history, cookies, site storage, credentials | Direct privacy harm and account takeover if exposed |
| Separation between identities and sessions | The core product promise |
| The user's IP address while a route is active | Users rely on routes to hide it from sites |
| The update channel and signed data | Compromise gives code execution on every install |
| Signing keys (installer, update responses, components) | Same as the update channel |
| Build and release infrastructure | Can be used to insert code into releases |

## Trust boundaries

```
 web content ──► renderer (sandboxed) ──mojo──► browser process ◄── our signed updates/data
                                                   │   ▲
 extensions ─────────────── (permission model) ────┘   │
                                                        │
 network: sites, proxies, DNS ◄── network service ──────┘
 local machine: OS, other users, same-user processes
 supply chain: upstream, crates, build hosts, CI
```

- **The renderer is untrusted.** Everything that crosses from a renderer into the browser process is validated. No privacy or security decision depends on renderer-supplied data.
- **Extensions are trusted only as far as their granted permissions.**
- **Downloaded data is trusted only when signed** by a key the build pins.

## Adversaries

| Adversary | Capabilities | Controls | Residual risk |
|---|---|---|---|
| Malicious or tracking website | Runs JavaScript, reads web-exposed APIs, sets storage, embeds third parties | Upstream sandbox and site isolation (never weakened); blocking; third-party cookie blocking and storage partitioning; fingerprinting policy in Blink | Novel fingerprinting surfaces; first-party tracking a site performs about its own users |
| Compromised renderer | Arbitrary code inside the renderer sandbox; can send any mojo message | Policy is browser-authoritative; every new mojom interface gets IPC security review; renderer-reported events are display-only | Upstream sandbox escapes (fixed via Chromium security releases) |
| Network observer | Sees and can tamper with unencrypted traffic; sees DNS and connection metadata | HTTPS-First, HSTS, DNS-over-HTTPS; certificate verification unchanged | Which sites the user visits is visible without a route |
| Route operator (proxy provider) | Sees every destination for routed traffic; can attempt TLS interception (fails certificate checks) | HTTPS end to end; fail-closed routes; UI states what the provider can see | Full browsing metadata for routed identities |
| Malicious extension | Whatever permissions the user grants; can read pages across identities | Upstream permission model; extensions disabled in Private and Ghost unless allowed | An extension with broad host access defeats identity separation. Documented. |
| Malicious download | Executables and documents delivered by sites | Upstream download protections; file-type policies (mirrored security component); Safe Browsing pending its decision gate ([licensing.md](licensing.md#release-gates)) | Reduced protection until the Safe Browsing gate is resolved |
| Local attacker, other user or physical access after the fact | Reads files on disk | Ghost and Private data kept in memory; OS account and disk encryption | Page file, hibernation file, crash dumps, and OS DNS client cache are out of our control |
| Local attacker, same user, while running | Arbitrary code as the user | None: out of scope | Full compromise |
| Supply-chain attacker | Compromises a dependency, CI runner, build host or developer account | See [Supply chain](#supply-chain) | Upstream Chromium itself is trusted |
| Attacker controlling our servers | Serves arbitrary update or component responses | Offline and HSM-held signing keys; clients verify signatures and reject version rollback | Denial of updates |

## Invariants

A change that violates one of these is rejected regardless of its benefit.

1. **No weakening of upstream security features.** The sandbox, site isolation, certificate verification, mixed-content blocking and download protections stay at upstream strength or stronger. That includes flags and field-trial overrides.
2. **The Rule of 2 applies to our code.** Untrusted input, an unsafe language, and high privilege: never all three. Parsers of downloaded data run in Rust, or in a sandboxed utility process.
3. **The browser process decides policy.** Renderers apply what they are told.
4. **Signed or rejected.** Every downloaded update, list or data component is verified against a key pinned in the binary.
5. **No new network destination without review.** The egress allowlist is part of the security review.

## Supply chain

- **Pinned inputs.**
  - Chromium comes from an exact release tag.
  - Rust crates are vendored and pinned by content.
  - CI actions are pinned by commit SHA.
  - The toolchain version is recorded in `build/requirements.json`.
- **Dependency review.** New dependencies go through licensing and security review ([CONTRIBUTING.md](../CONTRIBUTING.md#review-requirements)). Vendored updates are reviewed as diffs.
- **CI isolation.**
  - Pull-request CI never runs on the release builder.
  - Self-hosted runners never run pull requests from forks without a maintainer's approval label, because a public repository with self-hosted runners is otherwise a remote-code-execution vector.
- **Release builds** are produced from a clean checkout of a tag, on a builder dedicated to releases.
  - They are signed with hardware-held keys ([signing](signing/README.md)). Releases to Stable require two maintainers' approval.
  - Each release publishes an SBOM (SPDX) and build provenance (SLSA).
- **Reproducibility** is a goal we track, not a claim we make. Chromium has deterministic-build infrastructure. We'll measure how close our release builds get before advertising anything.

## Update security

- **Browser updates.** Installers are Authenticode-signed, and update responses are signed with CUP. Clients refuse versions older than the installed one, except for an explicit, signed rollback instruction.
- **Components.** They are CRX3-signed with our key. Mirrored Google security components (CRLSet, certificate transparency data, file-type policies) keep Google's signatures, which the client verifies unchanged.
- **Keys.**
  - The publisher key, which signs every update package, is held in a TPM or a hardware token and never exists as a file on a build machine. A backup publisher key, pinned beside it and kept offline elsewhere, means losing the primary doesn't strand installed browsers ([signing](signing/README.md)).
  - The CUP key signs each update response live, over the client's nonce, so it is the one signing key on the update server. Its versions let it rotate.
  - Key rotation is supported by shipping the next public key in a signed release before it's used.
- **Server compromise.** Our update servers hold only the CUP key. An attacker who controls them can withhold updates, but can't serve malicious ones without the publisher key.

## Security response

- **Release SLA.** Upstream security releases ship within the SLA in [roadmap.md](roadmap.md#security-release-sla), and fixes for our own code are released as soon as they're ready.
- **Reporting.** Reports go through [SECURITY.md](../SECURITY.md).
- **Advisories** are published after users have had time to update.

## Non-goals

- **Anonymity** against an adversary who observes both ends of a connection, or the user's network. Use Tor Browser.
- **Protection from malware** running as the same user.
- **Protection of data the user deliberately shares with a site**, such as signing in, uploading or typing.
- **Defeating server-side tracking** that a site performs about its own logged-in users.
- **Hiding that a user uses Ghost.** See [privacy-model.md](privacy-model.md#limitations).
