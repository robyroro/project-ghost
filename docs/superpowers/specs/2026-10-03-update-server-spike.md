# Update server: progress notes

- Date: 2026-10-03
- Design: [2026-10-03-update-server-design.md](2026-10-03-update-server-design.md); plan: [2026-10-03-update-server.md](../plans/2026-10-03-update-server.md)
- Repository: [project-ghost-update-server](https://github.com/robyroro/project-ghost-update-server)

## State

**Done (plan Tasks 1 to 12, 14 Steps 1 to 3, and 15):**
- The new repository holds the protocol, CUP signing (OpenSSL through `cryptography`), the manifest, the service, `ghost-update-admin`, the release CLI, the deployment files, `provision.sh` and `tools/deploy.py`. Each was written test-first.
- CI passes on Ubuntu 24.04 with Python 3.11 and `cryptography` 38.0.4 (Debian 12's versions) and on Windows.
- The fixtures copied from `//ghost` match the browser's files byte for byte, checked with `GHOST_WEBOPS` set.
- `//ghost`: `ghost_update_url` is a GN argument with the loopback default; `tools/update_smoke.py` gained `--server`, `--server-ssh` and `--online-installer`. After the change, B's local end-to-end test passed again in Windows Sandbox (2026-10-03, every step `ok`).

**Deferred (plan Tasks 13, 14 Step 4, 16 and 17):** by decision on 2026-10-03, the VPS isn't created yet. Nothing has run on a Linux server: `provision.sh`, Caddy's configuration, the nftables rules, the systemd hardening, `find-address`, and the remote and online end-to-end runs are untested beyond their configuration tests. Phase 2's exit criterion "an update shipped end to end to test machines" stays open until they pass.

The VPS can be temporary: billed by the hour, provisioned with `tools/deploy.py`, deleted after the runs, and provisioned again the same way when the domain and the production keys exist.

## Measurements

| Build | Time |
|---|---|
| `out/updater` after `ghost_update_url` became a GN argument (value unchanged) | 2 min 22 s. The plan expected under a minute; the action count wasn't checked. |

## Mutation checks (plan Task 11)

Each change was made, the named tests run, and the change undone.

| Change | Failed |
|---|---|
| The service's 200 response without `X-Cup-Server-Proof` | `test_an_update_is_offered_and_signed` |
| The Caddyfile without `header_up -X-Forwarded-For` | `test_the_proxy_passes_no_client_address` |
| The protocol offering a release to a client already at that version (`>=` made `>`) | `test_the_same_or_a_newer_version_gets_noupdate` |
| The service's request log back on (`log_message` calling the default) | `test_nothing_from_a_request_is_written`: the client's address reached the output |
