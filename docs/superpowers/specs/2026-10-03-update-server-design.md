# Update server: design

- Status: approved in discussion 2026-10-03, waiting for review of this document
- Phase 2, sub-project C ([roadmap](../../roadmap.md#phase-2-release-engineering))
- Depends on sub-project B ([branded updater](2026-10-02-branded-updater-design.md))

## Goal

Ghost's update server answers the updater's Omaha 4 checks with CUP-signed responses and serves the release packages, from a VPS on the internet. It keeps no record of who asked. A test machine installs from the offline installer and takes an update published to the server: the Phase 2 exit criterion "an update shipped end to end to test machines".

## Decisions

These were settled in discussion on 2026-10-03:

- **A VPS of our own.** One small server (for example Hetzner, about €5 a month with 20 TB of traffic) runs the service behind Caddy.
  - We control every log on it, so "the update server doesn't retain IP addresses" ([privacy-model.md](../../privacy-model.md#data-the-browser-sends)) is a promise we can enforce and test.
  - Serverless hosting (Cloudflare Workers and R2) was rejected: client IPs would pass through a third party, whose logging policy would become part of the promise. A VPS with a separate CDN for downloads was rejected for now: two providers, and two logging policies in the promise.
- **A separate repository**, `project-ghost-update-server`, as the technical plan requires for server-side code. `//ghost` keeps the client and the test server in `tools/`.
- **The domain is chosen later.** The test identity's server is reached by the VPS's IP address over TLS from Caddy's internal CA. The domain becomes one change in `branding/updater.gni` and the Caddyfile, then Caddy switches to ACME.
- **Our own service, not an existing Omaha server.**
  - Crystalnix's omaha-server speaks Omaha 3 (XML), needs Django and Postgres, and collects usage statistics.
  - Brave's go-update serves extensions only.
  - Precomputed responses are impossible: CUP signs each response over the request's hash and the client's nonce.
- **OpenSSL signs in production.** The service signs CUP with the `cryptography` package. `tools/ecdsa_p256.py` is pure Python and not constant-time; on a network-facing server its timing could leak the key. It stays the independent verifier in tests.

## What the client already does (sub-project B)

- Sends update checks to `update_check_url` from `branding/updater.gni`, with `cup2key` and `cup2hreq` in the query and a scrubbed JSON body ([the update request](../../privacy-model.md#the-update-request)).
- Verifies the CUP signature with Ghost's key (`branding/cup_key.h`, key version 1) and rejects the response otherwise.
- Downloads the CRX3 named in the response, checks its size and SHA-256, requires Ghost's publisher proof, and runs the installer inside.
- Sends no event requests.

**The overrides file doesn't apply.** Upstream's `overrides.json` changes the update URL only in `updater_test.exe`, which links `constants_test`; `updater.exe` links `constants_prod` and ignores it. The URL is therefore a build input (below).

## Components

### The new repository

| Path | What it is |
|---|---|
| `server/protocol.py` | Parses an Omaha 4 request, decides each app's answer, builds the response. No I/O. |
| `server/cup.py` | Signs a response for a request (ECDSA P-256 via `cryptography`), in the format of [cup.md](https://chromium.googlesource.com/chromium/src/+/refs/tags/152.0.7977.149/docs/updater/cup.md). |
| `server/manifest.py` | Loads and validates `releases.json`; keeps the last good manifest when a new one is invalid. |
| `server/main.py` | The HTTP service: `POST /update` on `127.0.0.1:8484`, stdlib `http.server`. |
| `publish/release.py` | The release CLI, run on the build machine. |
| `deploy/` | `provision.sh`, the systemd unit, the Caddyfile, the nftables rules, `ghost-update-admin`. |
| `tests/` | `unittest`, with fixtures copied from `//ghost`. |
| `.github/workflows/` | Tests and lint on every push, as in `//ghost`. |

It is licensed MPL-2.0 like `//ghost`, with DCO sign-off.

### Changes in `//ghost`

- **`branding/updater.gni`:** `update_check_url` comes from a new GN argument, `ghost_update_url`, whose default stays `http://127.0.0.1:8484/update` for the local end-to-end test.
- **`tools/update_smoke.py`:** a `--server <url>` mode for the remote end-to-end test (below).
- **`docs/`:** the privacy model's server section, the architecture's update section, the build guide, the roadmap.

No patch to Chromium changes.

## Answering a check

1. The updater sends `POST https://<host>/update?cup2key=<version>:<nonce>&cup2hreq=<hash>`.
2. Caddy terminates TLS and proxies to `127.0.0.1:8484`. It removes `X-Forwarded-For` and adds nothing about the client, so the service never sees a client address: every connection comes from loopback.
3. The service refuses a body over 64 KiB (413), a request without `cup2key` (400), or JSON that isn't an Omaha 4 request (400). The updater treats any non-200 as an error and retries with its own backoff. An unsigned response is never sent.
4. For each app in the request:

   | Request | Answer |
   |---|---|
   | Known app ID, `version` lower than the active release | `status: ok`, `nextversion`, and B's pipeline: `download` (URL, size, SHA-256), then `crx3` running the installer with its arguments |
   | Known app ID, no `version`, an empty one or `0.0.0.0` (an install from the online installer) | the same, an install of the active release |
   | Known app ID, `version` equal or higher | `updatecheck.status: noupdate`. The server never offers a lower version. |
   | App ID in the manifest with no release (the updater's own) | `updatecheck.status: noupdate` |
   | Unknown app ID | `status: error-unknownApplication` |
   | An app without `updatecheck` (an event or a ping) | `status: ok` with no `updatecheck`, and nothing recorded |

5. The service signs the response body and sends `X-Cup-Server-Proof`. Responses start with the `)]}'` prefix, as upstream's servers and B's test server do.
6. The updater downloads `https://<host>/releases/<file>.crx3`, served by Caddy straight from `/srv/releases/`, with range requests.

Versions are compared as four dotted integers. A version that isn't one is an invalid request (400).

## No record of who asked

| Where | What happens |
|---|---|
| Caddy | No access log: the Caddyfile has no `log` directive. Its own error log is configured without request fields, so without the remote address. A test checks both. |
| The service | Never sees an address. It never logs request bodies, `sessionid` or `requestid`; it logs only errors that carry no client data, such as "request without apps". |
| Rate limiting | nftables meters per source address, in kernel memory, expiring after 60 seconds. Nothing is written to disk. |
| journald | Receives only the service's and Caddy's messages above. |
| SSH | Records the administrators' own logins, which are not users of the browser. |

**Tested end to end:** after the remote end-to-end run, the test searches the whole VPS (journald, `/var/log`, Caddy's data directory) for the test machine's public address and fails if it appears.

## Publishing a release

`publish/release.py --crx <file> --appid <id> --version <v> --host <ssh host>`:
1. **Checks locally** that the CRX3 carries Ghost's publisher proof, that it contains the installer it names, and that the version follows [ADR 0007](../../adr/0007-version-numbers.md).
2. **Uploads** the package over SSH to `/srv/releases/staging/`.
3. **Activates** it with `ghost-update-admin activate` on the server, which:
   - refuses a version that isn't higher than the active one;
   - recomputes the size and SHA-256;
   - writes the new `releases.json` beside the old one and renames it into place.

The service reloads `releases.json` when its modification time changes. The last 3 releases stay on disk; `ghost-update-admin list` shows them and which is active.

`releases.json` is the server's whole state: the known app IDs and, for each that has one, the active release's version, file, size, SHA-256, installer name and installer arguments. Staged rollout and the halt switch (sub-project E) build on it.

## The VPS

- **System:** Debian 12, `unattended-upgrades`, SSH with keys only and no root login, nftables open on 22, 80 (ACME, once there's a domain) and 443.
- **The service** runs as a user with no shell, under systemd: `ProtectSystem=strict`, `NoNewPrivileges`, `PrivateTmp`, `RestrictAddressFamilies`, `SystemCallFilter=@system-service`, read-only access to `/srv/releases`.
- **The CUP private key** reaches it through `LoadCredential=`. The file is `root:root 0600`; the service can't read it from disk itself. It refuses to start without the key or without a valid manifest.
- **Provisioning:** `deploy/provision.sh` is idempotent. The administrator creates the VPS, adds an SSH key and runs it; it installs Caddy, the service and the firewall, then checks its own result. Run twice, the second run must change nothing.

**The keys are B's test keys.** Their private halves are committed in `//ghost`'s `test/updater/`, so anyone could build a valid response and package for the test identity. Using one would also take a TLS interception or control of the VPS, and only test machines trust these keys. Sub-project D replaces them with production keys, the publisher key held offline. Generating fresh test keys now would mean a rebuild for no gain, since a test key has to live somewhere.

**Why the server can't publish an update on its own:** it holds the CUP key, which it needs to sign live, but not the publisher key. A compromised server can withhold updates but not serve a malicious one ([threat-model.md](../../threat-model.md)), once D moves the publisher key offline.

## Errors

| Condition | Behavior |
|---|---|
| Invalid request (size, missing `cup2key`, bad JSON or version) | 400 or 413, no body. Logged without client data. |
| `releases.json` missing or invalid at start | The service doesn't start. |
| `releases.json` invalid on reload, or naming a file that is missing or of the wrong size | The last good manifest stays active; the error is logged. |
| CUP key missing | The service doesn't start. |
| Slow client | Caddy closes the connection after 10 seconds. |

## Tests

**In the new repository:**
- **Protocol:** the decision table above, including the request B captured (`test/updater/captured_request.json`).
- **CUP:** every response the service produces verifies with `tools/ecdsa_p256.py`'s pure-Python verifier, copied in as the reference, and the signer reproduces `test/updater/cup_vector.json`.
- **Shared fixtures** are copied from `//ghost`, and a test pins their SHA-256 values, so that they can't drift silently.
- **No client data in output:** requests carrying marker values in `sessionid` and `requestid` are sent to the running service; neither marker may appear in anything it writes.
- **Configuration:** the Caddyfile has no `log` directive and removes `X-Forwarded-For`; the unit file has the hardening options above.
- **Manifest and publishing:** atomic reload, keeping the last good manifest, and `release.py`'s refusals (another publisher key, a lower version, a CRX without its installer).
- **Mutation checks:** removing the signature, the `X-Forwarded-For` removal or the version check must each make a test fail.

**The remote end-to-end test** (`update_smoke.py sandbox --server https://<VPS address>`):
- **Inputs:** an offline installer for respin `-1` from a new static output directory, `out/updater_remote`, built with `ghost_update_url` set to the VPS. Its first build takes about 45 minutes. `out/updater` keeps the loopback URL; no output directory is renamed.
- **Before the run:** respin `-2` is published to the VPS with `release.py`.
- **In the Sandbox,** which gets network access for this mode: Caddy's root certificate is imported into the machine's trusted roots, then the steps are those of B's test (install, update to `-2`, launch, uninstall and the updater's removal).
- **Privacy:** the client's allow-list is already proven by B's local test, with the same client. This test instead checks the VPS for the test machine's address afterwards, over SSH.
- **The online installer:** a second, shorter run installs from `UpdaterSetup.exe` with no payload and the same install arguments. The server must answer the install request with the active release, `-2`.

## Done when

- The VPS is provisioned by `provision.sh`, and a second run changes nothing.
- The remote end-to-end test passes, offline and online installers, and the VPS holds no trace of the test machine's address.
- The repository's tests pass in CI, and the mutation checks above are recorded.
- The documentation is updated:
  - [privacy-model.md](../../privacy-model.md#data-the-browser-sends) says how the server keeps no addresses, and links the server's repository;
  - [architecture.md](../../architecture.md#updates-and-signed-data) describes the server;
  - the build guide explains `ghost_update_url` and the remote test;
  - the roadmap marks C done, with Phase 2's first exit criterion.

## Out of scope

- **Components** (filter lists, CRLSet and other security data): their own sub-project, on this server.
- **Production keys, Authenticode signing and tagged installers:** sub-project D.
- **The domain**, and with it ACME: chosen by the user, then one configuration change.
- **Staged rollout and the halt switch:** sub-project E.
- **The updater updating itself:** the server answers the updater's own checks with `noupdate` (its app ID is in the manifest with no release). Packaging the updater as an update comes with the release pipeline, sub-project E.
- **An update applied while the browser runs** (`new_chrome.exe`): a client behavior the server doesn't change. Sub-project E checks it.
- **More than one channel, system-level installs, macOS and Linux.**
- **A second server, monitoring and backups:** before the public alpha, not for test machines.
