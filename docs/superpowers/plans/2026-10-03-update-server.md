# Update Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ghost's update server answers the updater's Omaha 4 checks from a VPS, signs every answer with CUP, keeps no record of client addresses, and ships an update end to end to a test machine.

**Architecture:** A new repository, `project-ghost-update-server`, holds a stdlib Python package (`ghost_update`) that signs with `cryptography`, the deployment (Caddy, nftables, systemd, `provision.sh`) and its tests. `//ghost` gains a GN argument for the update URL and a remote mode in `tools/update_smoke.py`. Spec: [2026-10-03-update-server-design.md](../specs/2026-10-03-update-server-design.md).

**Tech Stack:** Python 3.11 (Debian 12's), `cryptography` 38 (Debian 12's `python3-cryptography`), Caddy 2.6 (Debian 12's), nftables, systemd, `unittest`, Windows Sandbox, OpenSSH.

---

## Conventions

- `WEBOPS` = `C:\Users\robyv\Desktop\DLU\webops` (`/c/Users/robyv/Desktop/DLU/webops` in Git Bash), `SRC` = `$WEBOPS/chromium/src`.
- `SRV` = `C:\Users\robyv\Desktop\DLU\project-ghost-update-server` (`/c/Users/robyv/Desktop/DLU/project-ghost-update-server`).
- `VPS` = the server's public IPv4 address, known after Task 13. Examples use `203.0.113.5` (a documentation address).
- Every source file starts with the MPL-2.0 notice, as in `//ghost`: `#` comments for Python, shell, Caddyfile, nftables, systemd and sudoers files.
- Commits: small, no AI trailers, the user is the sole author. In `SRV`, `git commit -s` (DCO).
- Tests in `SRV`: `python -m unittest discover -s tests -t . -v`. Lint: `python tools/lint.py`. Lint checks tracked files only, so run it after `git add`.
- Locally, `cryptography` is installed once for the user's Python: `python -m pip install --user cryptography`. CI pins 38.0.4, the version the server runs.
- Never rename a Chromium output directory. Never edit `tools/` in `WEBOPS` while a Sandbox run is in progress (the Sandbox maps it live). Leave at least 3 minutes between two Sandbox runs: a Sandbox started seconds after the last one closed never ran its script (spike notes of sub-project B).

## File map

**New repository `SRV`:**

| File | Responsibility |
|---|---|
| `ghost_update/__init__.py` | Package marker |
| `ghost_update/identity.py` | The test identity's values: CUP key version, publisher key hash, app IDs |
| `ghost_update/protocol.py` | Omaha 4 parsing, the decision table, the response. No I/O. |
| `ghost_update/cup.py` | Loads the CUP key; signs responses with OpenSSL; validates `cup2key` |
| `ghost_update/manifest.py` | `releases.json`: parse, validate, serialize, atomic write, reload with last-good fallback |
| `ghost_update/service.py` | The HTTP service on loopback |
| `ghost_update/admin.py` | `ghost-update-admin`: `init`, `activate`, `list`, `find-address` |
| `ghost_update/release.py` | Build-machine CLI: checks a CRX3, uploads it, activates it |
| `ghost_update/reference/ecdsa_p256.py`, `crx3.py` | Copies of `//ghost`'s pure-Python code: the release CLI's check and the tests' reference verifier |
| `deploy/provision.sh` | Idempotent provisioning of a Debian 12 VPS |
| `deploy/ghost-update.service` | systemd unit with hardening |
| `deploy/Caddyfile` | TLS, the proxy without client addresses, package downloads, no access log |
| `deploy/nftables.conf` | Firewall and per-address rate limit in kernel memory |
| `deploy/server.json` | Service configuration template |
| `deploy/ghost-update-admin` | Wrapper installed in `/usr/local/sbin` |
| `deploy/sshd.conf`, `deploy/sudoers`, `deploy/20auto-upgrades` | SSH keys only and no root login; the administrator's sudo; automatic security updates |
| `tools/deploy.py` | Copies a bundle to the VPS and runs `provision.sh` |
| `tools/lint.py` | MPL notice, LF endings, Markdown links |
| `tests/…` | One test module per unit; `tests/fixtures/` copied from `//ghost` |
| `.github/workflows/tests.yml` | Tests and lint on every push |

**`WEBOPS`:**

| File | Change |
|---|---|
| `branding/updater.gni` | `update_check_url` comes from the GN argument `ghost_update_url` |
| `tools/installer_smoke.py` | `sandbox_config(..., networking=False)` |
| `tools/update_smoke.py` | `--server`, `--server-ssh`, `--online-installer` |
| `tools/tests/test_update_smoke.py` | Tests for the above |
| `docs/…` | Privacy model, architecture, build guide, testing, roadmap, spike notes, spec status |

---

### Task 0: Preconditions

- [ ] **Step 1: State of `WEBOPS`.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git status --short && git log --oneline -1
```

Expected: clean, at or after the commit that added this plan.

- [ ] **Step 2: Local `cryptography`.**

```bash
python -m pip install --user cryptography && python -c "import cryptography; print(cryptography.__version__)"
```

Expected: a version number.

- [ ] **Step 3: Ask the user two administrative questions; wait for both answers.**
  1. "Create the GitHub repository `robyroro/project-ghost-update-server` (public, MPL-2.0, empty), or may I create it with `gh repo create robyroro/project-ghost-update-server --public`?"
  2. "Create a VPS: Debian 12, x86-64, the smallest size (for example Hetzner CX22), with your SSH public key for root. Send me its IPv4 address."

  Tasks 1 to 11 don't need either answer; Task 12 needs the first, Task 13 the second.

---

### Task 1: Repository skeleton, lint and CI

**Files:**
- Create: `SRV/README.md`, `SRV/LICENSE`, `SRV/.gitattributes`, `SRV/.gitignore`, `SRV/tools/lint.py`, `SRV/tests/__init__.py`, `SRV/tests/test_lint.py`, `SRV/.github/workflows/tests.yml`

- [ ] **Step 1: Create the repository.**

```bash
mkdir -p /c/Users/robyv/Desktop/DLU/project-ghost-update-server && cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && git init -q -b main
cp /c/Users/robyv/Desktop/DLU/webops/LICENSE LICENSE
mkdir -p ghost_update/reference tests/fixtures deploy tools .github/workflows
```

- [ ] **Step 2: `.gitattributes` and `.gitignore`.**

`.gitattributes`:
```
* text=auto eol=lf
*.crx3 binary
```

`.gitignore`:
```
__pycache__/
*.pyc
/build/
```

- [ ] **Step 3: `README.md`.**

```markdown
# Project Ghost update server

The update server of [Project Ghost](https://github.com/robyroro/project-ghost), a privacy-first browser built on Chromium. It answers the browser updater's Omaha 4 checks with CUP-signed responses, serves the release packages, and keeps no record of who asked.

- **Design:** [the update server's design](https://github.com/robyroro/project-ghost/blob/main/docs/superpowers/specs/2026-10-03-update-server-design.md), in the browser's repository.
- **What the browser sends:** [the update request](https://github.com/robyroro/project-ghost/blob/main/docs/privacy-model.md#the-update-request).

## Layout

| Path | What it is |
|---|---|
| `ghost_update/` | The service, `ghost-update-admin`, and the release CLI |
| `deploy/` | Provisioning of a Debian 12 server: Caddy, nftables, systemd |
| `tools/deploy.py` | Deploys to a server from the build machine |
| `tests/` | Tests; `tests/fixtures/` holds files copied from the browser's repository |

## Tests

    python -m pip install cryptography
    python -m unittest discover -s tests -t . -v
    python tools/lint.py

## Deploying

    python tools/deploy.py --host root@<address> --address <address> --cup-key <key file>

The first run, as root on a fresh Debian 12 server, creates the administrator `ghost` and turns off root's SSH login; later runs use `--host ghost@<address>`. Each run changes only what differs.

## Publishing a release

    python -m ghost_update.release --crx <package.crx3> --appid <app ID> --version <version> --host ghost@<address>

## Test identity

Until the final product name and production keys exist (the browser's Phase 2, sub-project D), the server uses the browser's test keys, whose private halves are public in the browser's repository. Only test machines trust them.

## License

MPL-2.0. Contributions are accepted under the [Developer Certificate of Origin](https://developercertificate.org/) (`git commit -s`).
```

- [ ] **Step 4: Write the failing lint test.** `tests/__init__.py` holds only the MPL notice. `tests/test_lint.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest

from tools import lint


class LintTest(unittest.TestCase):
    def test_notice_found_in_any_comment_style(self):
        notice = ("# This Source Code Form is subject to the terms of the Mozilla Public\n"
                  "# License, v. 2.0. If a copy of the MPL was not distributed with this\n"
                  "# file, You can obtain one at https://mozilla.org/MPL/2.0/.\n")
        self.assertTrue(lint.has_mpl_notice(notice))
        self.assertTrue(lint.has_mpl_notice("#!/bin/bash\n" + notice))
        self.assertFalse(lint.has_mpl_notice("print('hi')\n"))

    def test_the_repository_is_clean(self):
        self.assertEqual(lint.lint(lint.REPO_ROOT), [])


if __name__ == "__main__":
    unittest.main()
```

Run: `python -m unittest tests.test_lint -v`. Expected: `ModuleNotFoundError: No module named 'tools'`.

- [ ] **Step 5: `tools/__init__.py` (MPL notice only) and `tools/lint.py`.**

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Repository hygiene checks, as in the browser's tools/lint.py.

- Every source file carries the MPL-2.0 notice (Exhibit A).
- Text files use LF line endings.
- Relative links in Markdown point at files that exist.

Only files tracked by git are checked. Usage: python tools/lint.py
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote

REPO_ROOT = Path(__file__).resolve().parent.parent
MPL_NOTICE = ("This Source Code Form is subject to the terms of the Mozilla Public License, "
              "v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain "
              "one at https://mozilla.org/MPL/2.0/.")
HEADER_EXTENSIONS = {".py", ".sh", ".service", ".conf"}
HEADER_FILES = {"Caddyfile", "ghost-update-admin", "sudoers", "20auto-upgrades"}
HEADER_SEARCH_LINES = 15
_COMMENT_MARKERS = re.compile(r"^\s*(#!.*|#|//)")
_MD_LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)\)")


def tracked_files(root: Path) -> list[str]:
    out = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], capture_output=True,
                         check=True).stdout
    return [p for p in out.decode("utf-8").split("\0") if p]


def has_mpl_notice(text: str) -> bool:
    head = text.splitlines()[:HEADER_SEARCH_LINES]
    stripped = " ".join(_COMMENT_MARKERS.sub("", line).strip() for line in head)
    return MPL_NOTICE in " ".join(stripped.split())


def broken_links(md_path: Path, text: str) -> list[str]:
    broken = []
    for target in _MD_LINK.findall(text):
        if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.IGNORECASE):
            continue
        path_part = unquote(target.partition("#")[0])
        if path_part and not (md_path.parent / path_part).exists():
            broken.append(target)
    return broken


def lint(root: Path) -> list[str]:
    problems = []
    for rel in tracked_files(root):
        path = root / rel
        if not path.is_file():
            continue
        data = path.read_bytes()
        if b"\0" in data[:8192]:
            continue
        if b"\r\n" in data:
            problems.append(f"{rel}: CRLF line endings")
        text = data.decode("utf-8", "replace")
        name = Path(rel).name
        needs_notice = (Path(rel).suffix in HEADER_EXTENSIONS or name in HEADER_FILES)
        if needs_notice and not rel.startswith("tests/fixtures/") and not has_mpl_notice(text):
            problems.append(f"{rel}: missing the MPL-2.0 notice in the first "
                            f"{HEADER_SEARCH_LINES} lines")
        if rel.endswith(".md"):
            problems += [f"{rel}: broken relative link {t}" for t in broken_links(path, text)]
    return problems


def main() -> int:
    problems = lint(REPO_ROOT)
    for problem in problems:
        print(problem)
    print(f"lint: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 6: The CI workflow,** `.github/workflows/tests.yml`. The action SHAs are those `//ghost`'s `tooling.yml` pins.

```yaml
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Tests and lint. The service runs on Debian 12 (Python 3.11, cryptography
# 38.0.4); the release and deploy tools run on the Windows build machine.
name: tests

on:
  push:
  pull_request:

permissions:
  contents: read

concurrency:
  group: tests-${{ github.ref }}
  cancel-in-progress: true

jobs:
  test:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-24.04, windows-2025]
    runs-on: ${{ matrix.os }}
    timeout-minutes: 15
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1  # v7.0.1
        with:
          persist-credentials: false
      - uses: actions/setup-python@5fda3b95a4ea91299a34e894583c3862153e4b97  # v7.0.0
        with:
          python-version: "3.11"
      - name: Install cryptography (Debian 12's version)
        run: python -m pip install cryptography==38.0.4
      - name: Tests
        run: python -m unittest discover -s tests -t . -v
      - name: Lint
        run: python tools/lint.py
```

- [ ] **Step 7: Run the tests and lint; they pass. Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && git add -A && python -m unittest discover -s tests -t . 2>&1 | tail -1 && python tools/lint.py
git commit -q -s -m "repository: skeleton, lint and CI

The update server of Project Ghost, MPL-2.0 like the browser. Lint checks
the license notice, LF endings and Markdown links; CI runs the tests on
Ubuntu, with the cryptography version Debian 12 ships, and on Windows,
where the release and deploy tools run."
```

Expected: `OK`, `lint: 0 problem(s)`.

---

### Task 2: Identity, reference code and fixtures

**Files:**
- Create: `ghost_update/__init__.py`, `ghost_update/identity.py`, `ghost_update/reference/__init__.py`, `ghost_update/reference/ecdsa_p256.py`, `ghost_update/reference/crx3.py`
- Create: `tests/fixtures/{cup_test_key.json,cup_vector.json,captured_request.json,crx_test_key.json}`, `tests/helpers.py`, `tests/test_fixtures.py`

- [ ] **Step 1: Copy the fixtures and reference code from `WEBOPS`.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && W=/c/Users/robyv/Desktop/DLU/webops
cp $W/test/updater/{cup_test_key.json,cup_vector.json,captured_request.json,crx_test_key.json} tests/fixtures/
cp $W/tools/ecdsa_p256.py $W/tools/crx3.py ghost_update/reference/
sed -i 's/^import ecdsa_p256$/from ghost_update.reference import ecdsa_p256/' ghost_update/reference/crx3.py
grep -n "ecdsa_p256$" ghost_update/reference/crx3.py
```

Expected: one line, `from ghost_update.reference import ecdsa_p256`. In both copied modules, add this line at the end of the module docstring:

```
Copied from the browser's tools/ at 8e5ba4f; keep the two in step.
```

- [ ] **Step 2: `ghost_update/__init__.py` and `ghost_update/reference/__init__.py`**, each with the MPL notice and a one-line docstring: `"""Ghost's update server (Phase 2, sub-project C)."""` and `"""The browser's pure-Python ECDSA and CRX3 code, for checks and tests."""`

- [ ] **Step 3: `ghost_update/identity.py`.**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The test identity (Phase 2). Sub-project D replaces the keys; the final
product name replaces the app IDs. Each value matches the browser's
branding/ files, which tests/test_fixtures.py checks through the copied keys."""

# The CUP key version the updater announces in cup2key (branding/cup_key.h).
CUP_KEY_VERSION = 1

# SHA-256 of the DER SubjectPublicKeyInfo of the key whose proof Ghost's
# updater requires on packages (branding/crx_publisher_key.h).
PUBLISHER_KEY_SHA256 = "c3fc14d7c68bc76a213a242a9a7395aa94f033a044ead5e3e54a0e0629330968"

# branding/updater.gni: browser_appid and updater_appid, lowercase.
BROWSER_APPID = "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}"
UPDATER_APPID = "{4b5a3a08-578b-4b1f-8b2b-26cb99b30c4f}"
```

- [ ] **Step 4: Write the failing tests.** `tests/helpers.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Shared by the tests: the fixtures and the reference CUP verifier."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from ghost_update.reference import ecdsa_p256

FIXTURES = Path(__file__).resolve().parent / "fixtures"
CUP_KEY_FILE = FIXTURES / "cup_test_key.json"
CRX_KEY_FILE = FIXTURES / "crx_test_key.json"
# RFC 6979's P-256 test key: a key Ghost's updater doesn't trust.
OTHER_KEY = 0xC9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721


def private_key(path: Path) -> int:
    return int(json.loads(path.read_text(encoding="utf-8"))["private_key"], 16)


def reference_verify(public: tuple[int, int], cup2key: str, request_body: bytes,
                     response_body: bytes, proof: str) -> bool:
    """The browser's tools/update_server.py cup_verify, as the updater checks a proof."""
    signature_hex, _, hash_hex = proof.partition(":")
    request_hash = hashlib.sha256(request_body).digest()
    if bytes.fromhex(hash_hex) != request_hash:
        return False
    inner = hashlib.sha256(request_hash + hashlib.sha256(response_body).digest()
                           + cup2key.encode()).digest()
    return ecdsa_p256.verify(public, inner,
                             *ecdsa_p256.parse_der_signature(bytes.fromhex(signature_hex)))
```

`tests/test_fixtures.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import hashlib
import os
import unittest
from pathlib import Path

from ghost_update import identity
from ghost_update.reference import ecdsa_p256
from tests.helpers import CRX_KEY_FILE, FIXTURES, private_key

# Each fixture, its path in the browser's repository, and its SHA-256 there.
SOURCES = {
    "cup_test_key.json": ("test/updater/cup_test_key.json",
                          "0a9d129f7d4d6780fa2bbbf544d2c5e4dc04490b4a060a8b6f7a4ee3cc60cc0d"),
    "cup_vector.json": ("test/updater/cup_vector.json",
                        "c77fe2cb86ff269fecb1af3b2db99b6987e55fb12b24748546b783fefacc8a8e"),
    "captured_request.json": ("test/updater/captured_request.json",
                              "20deb525f5329d02d60cd490c44399cfa9e40857153d8d61d5f23035c4d97dd4"),
    "crx_test_key.json": ("test/updater/crx_test_key.json",
                          "f918a7e5f05b0c0b1a96acdb6d632bbbd9ab3db34099407e28403877a5a26052"),
}


class FixturesTest(unittest.TestCase):
    def test_fixtures_are_the_pinned_copies(self):
        for name, (_, digest) in SOURCES.items():
            with self.subTest(name):
                self.assertEqual(hashlib.sha256((FIXTURES / name).read_bytes()).hexdigest(),
                                 digest)

    def test_fixtures_match_the_browser(self):
        """Set GHOST_WEBOPS to a checkout of the browser's repository to compare."""
        webops = os.environ.get("GHOST_WEBOPS")
        if not webops:
            self.skipTest("GHOST_WEBOPS is not set")
        for name, (source, _) in SOURCES.items():
            with self.subTest(name):
                self.assertEqual((FIXTURES / name).read_bytes(),
                                 (Path(webops) / source).read_bytes())

    def test_the_publisher_key_hash_is_the_test_key(self):
        public = ecdsa_p256.spki(ecdsa_p256.public_key(private_key(CRX_KEY_FILE)))
        self.assertEqual(hashlib.sha256(public).hexdigest(), identity.PUBLISHER_KEY_SHA256)


if __name__ == "__main__":
    unittest.main()
```

Run: `python -m unittest tests.test_fixtures -v`. Expected before Steps 1 to 3: errors; after them, the first and third pass and the second is skipped.

- [ ] **Step 5: Compare with the browser once, then commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && GHOST_WEBOPS=/c/Users/robyv/Desktop/DLU/webops python -m unittest tests.test_fixtures -v 2>&1 | tail -3
git add -A && python tools/lint.py && python -m unittest discover -s tests -t . 2>&1 | tail -1
git commit -q -s -m "fixtures: the browser's test keys, CUP vector and captured request

Copied from the browser's repository and pinned by SHA-256; with
GHOST_WEBOPS set, the test compares them with the browser's. The
browser's pure-Python ECDSA and CRX3 code is copied as the reference the
tests and the release check use."
```

Expected: `OK` with no skips in the first run; `OK` and `lint: 0 problem(s)` after.

---

### Task 3: The protocol

**Files:**
- Create: `ghost_update/protocol.py`, `tests/test_protocol.py`

- [ ] **Step 1: Write the failing test,** `tests/test_protocol.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import datetime
import json
import unittest

from ghost_update import protocol
from ghost_update.identity import BROWSER_APPID, UPDATER_APPID
from tests.helpers import FIXTURES

TODAY = datetime.date(2026, 10, 3)
BASE = "https://203.0.113.5/releases"
RELEASE = protocol.Release("152.0.7977.14902", "browser-152.0.7977.14902.crx3", 1000, "ab" * 32,
                           "mini_installer.exe", "--verbose-logging --do-not-launch-chrome")
RELEASES = {BROWSER_APPID: RELEASE, UPDATER_APPID: None}


def answers(apps: list[dict]) -> list[dict]:
    body = json.dumps({"request": {"protocol": "4.0", "apps": apps}}).encode()
    payload = protocol.respond(body, RELEASES, BASE, TODAY)
    assert payload.startswith(b")]}'\n")
    return json.loads(payload[5:])["response"]["apps"]


class DecisionTest(unittest.TestCase):
    def test_an_older_version_gets_the_release(self):
        [app] = answers([{"appid": BROWSER_APPID, "version": "152.0.7977.14901",
                          "updatecheck": {}}])
        check = app["updatecheck"]
        self.assertEqual((app["status"], check["status"], check["nextversion"]),
                         ("ok", "ok", "152.0.7977.14902"))
        download, crx3 = check["pipelines"][0]["operations"]
        self.assertEqual(download, {
            "type": "download", "size": 1000, "out": {"sha256": "ab" * 32},
            "urls": [{"url": f"{BASE}/browser-152.0.7977.14902.crx3"}]})
        self.assertEqual(crx3, {"type": "crx3", "in": {"sha256": "ab" * 32},
                                "path": "mini_installer.exe",
                                "arguments": "--verbose-logging --do-not-launch-chrome"})

    def test_an_install_gets_the_release(self):
        for app in ({"appid": BROWSER_APPID, "version": "0.0.0.0", "updatecheck": {}},
                    {"appid": BROWSER_APPID, "version": "", "updatecheck": {}},
                    {"appid": BROWSER_APPID, "updatecheck": {}}):
            with self.subTest(app):
                self.assertEqual(answers([app])[0]["updatecheck"]["nextversion"],
                                 "152.0.7977.14902")

    def test_the_same_or_a_newer_version_gets_noupdate(self):
        for version in ("152.0.7977.14902", "152.0.7977.14903", "153.0.0.0"):
            with self.subTest(version):
                [app] = answers([{"appid": BROWSER_APPID, "version": version,
                                  "updatecheck": {}}])
                self.assertEqual(app["updatecheck"], {"status": "noupdate"})

    def test_an_app_without_a_release_gets_noupdate(self):
        [app] = answers([{"appid": UPDATER_APPID, "version": "152.0.7977.149",
                          "updatecheck": {}}])
        self.assertEqual(app, {"appid": UPDATER_APPID, "status": "ok",
                               "updatecheck": {"status": "noupdate"}})

    def test_an_unknown_app(self):
        appid = "{00000000-0000-0000-0000-000000000000}"
        self.assertEqual(answers([{"appid": appid, "version": "1.0.0.0", "updatecheck": {}}]),
                         [{"appid": appid, "status": "error-unknownApplication"}])

    def test_app_ids_match_without_case_and_are_echoed(self):
        [app] = answers([{"appid": BROWSER_APPID.upper(), "version": "1.0.0.0",
                          "updatecheck": {}}])
        self.assertEqual((app["appid"], app["updatecheck"]["status"]),
                         (BROWSER_APPID.upper(), "ok"))

    def test_an_event_is_acknowledged_with_nothing_else(self):
        self.assertEqual(answers([{"appid": BROWSER_APPID, "version": "152.0.7977.14901",
                                   "event": [{"eventtype": 3}]}]),
                         [{"appid": BROWSER_APPID, "status": "ok"}])

    def test_the_request_captured_from_the_browser(self):
        request = json.loads((FIXTURES / "captured_request.json").read_text())["request"]
        payload = protocol.respond(json.dumps(request).encode(), RELEASES, BASE, TODAY)
        [app] = json.loads(payload[5:])["response"]["apps"]
        self.assertEqual(app["updatecheck"]["nextversion"], "152.0.7977.14902")

    def test_the_response_envelope(self):
        payload = protocol.respond(b'{"request":{"protocol":"4.0","apps":[]}}', RELEASES, BASE,
                                   TODAY)
        response = json.loads(payload[5:])["response"]
        self.assertEqual(response["protocol"], "4.0")
        self.assertEqual(response["daystart"]["elapsed_days"],
                         (TODAY - datetime.date(2007, 1, 1)).days)


class InvalidRequestTest(unittest.TestCase):
    def test_invalid_requests_are_refused(self):
        def app(version):
            return json.dumps({"request": {"protocol": "4.0", "apps": [
                {"appid": BROWSER_APPID, "version": version, "updatecheck": {}}]}}).encode()
        for body in (b"not json", b"\xff", b"[]", b'{"request": []}',
                     b'{"request": {"protocol": "3.0", "apps": []}}',
                     b'{"request": {"protocol": "4.0"}}',
                     b'{"request": {"protocol": "4.0", "apps": [{"version": "1.0.0.0"}]}}',
                     app("1.2.3"), app("a.b.c.d"), app(5), app("1.2.3.4.5"), app("1.2.3.-4")):
            with self.subTest(body):
                with self.assertRaises(protocol.InvalidRequest):
                    protocol.respond(body, RELEASES, BASE, TODAY)

    def test_messages_quote_nothing_from_the_request(self):
        body = json.dumps({"request": {"protocol": "4.0", "apps": [
            {"appid": BROWSER_APPID, "version": "9.9.9.MARKER", "updatecheck": {}}]}}).encode()
        with self.assertRaises(protocol.InvalidRequest) as caught:
            protocol.respond(body, RELEASES, BASE, TODAY)
        self.assertNotIn("MARKER", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
```

Run: `python -m unittest tests.test_protocol -v`. Expected: `ImportError: cannot import name 'protocol'`.

- [ ] **Step 2: Write `ghost_update/protocol.py`.**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Omaha 4 update checks: parse a request, answer each app, build the response.

No I/O: the service passes in the request body and the active releases.
"""

from __future__ import annotations

import datetime
import json
import re
from dataclasses import dataclass

RESPONSE_PREFIX = ")]}'\n"
NULL_VERSION = "0.0.0.0"  # what the updater sends for an app it is installing
_DAY_ZERO = datetime.date(2007, 1, 1)
_VERSION = re.compile(r"^[0-9]{1,9}(\.[0-9]{1,9}){3}$")


class InvalidRequest(ValueError):
    """A request the server refuses. Messages never quote client data."""


@dataclass(frozen=True)
class Release:
    version: str
    file: str
    size: int
    sha256: str
    installer: str
    arguments: str


def parse_version(text: object) -> tuple[int, ...]:
    if not isinstance(text, str) or not _VERSION.match(text):
        raise InvalidRequest("a version is not four dotted integers")
    return tuple(int(part) for part in text.split("."))


def parse_request(body: bytes) -> list[dict]:
    try:
        data = json.loads(body)
    except (ValueError, UnicodeDecodeError):
        raise InvalidRequest("the body is not JSON") from None
    request = data.get("request") if isinstance(data, dict) else None
    if not isinstance(request, dict):
        raise InvalidRequest("no request object")
    if request.get("protocol") != "4.0":
        raise InvalidRequest("not protocol 4.0")
    apps = request.get("apps")
    if not isinstance(apps, list) or not all(
            isinstance(app, dict) and isinstance(app.get("appid"), str) for app in apps):
        raise InvalidRequest("apps is not a list of apps with IDs")
    return apps


def answer(app: dict, releases: dict[str, Release | None], download_base: str) -> dict:
    """One app's answer. `releases` maps lowercase app IDs to their active release."""
    appid = app["appid"]
    if appid.lower() not in releases:
        return {"appid": appid, "status": "error-unknownApplication"}
    entry: dict = {"appid": appid, "status": "ok"}
    if "updatecheck" not in app:
        return entry  # an event or a ping: acknowledged, nothing recorded
    release = releases[appid.lower()]
    installed = parse_version(app.get("version") or NULL_VERSION)
    if release is None or installed >= parse_version(release.version):
        entry["updatecheck"] = {"status": "noupdate"}
        return entry
    entry["updatecheck"] = {
        "status": "ok", "nextversion": release.version,
        "pipelines": [{"pipeline_id": "full", "operations": [
            {"type": "download", "size": release.size, "out": {"sha256": release.sha256},
             "urls": [{"url": f"{download_base}/{release.file}"}]},
            {"type": "crx3", "in": {"sha256": release.sha256}, "path": release.installer,
             "arguments": release.arguments}]}]}
    return entry


def respond(body: bytes, releases: dict[str, Release | None], download_base: str,
            today: datetime.date) -> bytes:
    """The response body for a request body. Raises InvalidRequest."""
    apps = [answer(app, releases, download_base) for app in parse_request(body)]
    response = {"response": {"protocol": "4.0", "server": "ghost",
                             "daystart": {"elapsed_days": (today - _DAY_ZERO).days},
                             "apps": apps}}
    return (RESPONSE_PREFIX + json.dumps(response, separators=(",", ":"))).encode()
```

- [ ] **Step 3: Run the tests; they pass. Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest tests.test_protocol -v 2>&1 | tail -3
git add -A && python tools/lint.py && git commit -q -s -m "protocol: Omaha 4 answers without I/O

An app older than its active release gets the release; an install
(0.0.0.0 or no version) gets it too; the same or a newer version gets
noupdate, so the server never offers a downgrade. Unknown apps get
error-unknownApplication, events an acknowledgement. Errors quote
nothing from the request."
```

Expected: `OK`.

---

### Task 4: CUP

**Files:**
- Create: `ghost_update/cup.py`, `tests/test_cup.py`

- [ ] **Step 1: Write the failing test,** `tests/test_cup.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import unittest

from ghost_update import cup
from ghost_update.reference import ecdsa_p256
from tests.helpers import CUP_KEY_FILE, FIXTURES, private_key, reference_verify


class CupTest(unittest.TestCase):
    def setUp(self):
        self.key = cup.load_key(CUP_KEY_FILE)
        numbers = self.key.public_key().public_numbers()
        self.public = (numbers.x, numbers.y)

    def test_the_loaded_key_is_the_test_key(self):
        self.assertEqual(self.public, ecdsa_p256.public_key(private_key(CUP_KEY_FILE)))

    def test_the_browser_vector_verifies_with_the_loaded_key(self):
        vector = json.loads((FIXTURES / "cup_vector.json").read_text())
        self.assertTrue(reference_verify(self.public, vector["cup2key"],
                                         vector["request"].encode(),
                                         vector["response"].encode(), vector["proof"]))

    def test_proofs_verify_as_the_updater_checks_them(self):
        for request, response in ((b"{}", b")]}'\n{}"), (b"x" * 5000, b"y" * 7000)):
            proof = cup.proof(self.key, "1:12345", request, response)
            self.assertTrue(reference_verify(self.public, "1:12345", request, response, proof))
            self.assertFalse(reference_verify(self.public, "1:12345", request, response + b" ",
                                              proof))
            self.assertFalse(reference_verify(self.public, "1:12346", request, response, proof))

    def test_cup2key(self):
        for good in ("1:12345", "1:SUUSbBaXj4Q5AofrKJTxPrbrwU_XSvKjCY1jp_dvqec"):
            self.assertTrue(cup.valid_cup2key(good), good)
        for bad in ("", "1", "1:", "2:12345", "x:1", "1:a b", "1:" + "a" * 129, "01:1"):
            self.assertFalse(cup.valid_cup2key(bad), bad)


if __name__ == "__main__":
    unittest.main()
```

Run: `python -m unittest tests.test_cup -v`. Expected: `ImportError: cannot import name 'cup'`.

- [ ] **Step 2: Write `ghost_update/cup.py`.**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""CUP: signing update responses so the updater can trust them.

The updater (components/client_update_protocol/cup.cc) checks
    ECDSA-SHA256 over SHA-256(SHA-256(request) | SHA-256(response) | cup2key)
sent as X-Cup-Server-Proof: "<DER signature, hex>:<SHA-256(request), hex>".
OpenSSL signs, through `cryptography`: the browser's pure-Python signer isn't
constant-time, and a network-facing server must not leak its key in timing.
"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec

from ghost_update.identity import CUP_KEY_VERSION

_CUP2KEY = re.compile(r"^([1-9][0-9]{0,3}):[A-Za-z0-9_=-]{1,128}$")


def load_key(path: Path) -> ec.EllipticCurvePrivateKey:
    """A key file as the browser's tools/update_server.py writes it."""
    value = int(json.loads(Path(path).read_text(encoding="utf-8"))["private_key"], 16)
    return ec.derive_private_key(value, ec.SECP256R1())


def valid_cup2key(cup2key: str) -> bool:
    """`<key version>:<nonce>`, for the key version this server signs with."""
    match = _CUP2KEY.match(cup2key)
    return bool(match) and int(match.group(1)) == CUP_KEY_VERSION


def proof(key: ec.EllipticCurvePrivateKey, cup2key: str, request_body: bytes,
          response_body: bytes) -> str:
    request_hash = hashlib.sha256(request_body).digest()
    inner = hashlib.sha256(request_hash + hashlib.sha256(response_body).digest()
                           + cup2key.encode()).digest()
    signature = key.sign(inner, ec.ECDSA(hashes.SHA256()))
    return f"{signature.hex()}:{request_hash.hex()}"
```

- [ ] **Step 3: Run the tests; they pass. Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest tests.test_cup -v 2>&1 | tail -3
git add -A && python tools/lint.py && git commit -q -s -m "cup: sign responses with OpenSSL

Proofs verify with the browser's reference verifier, and the loaded test
key verifies the browser's CUP vector. A cup2key for another key version
is refused, since the updater couldn't verify the answer."
```

Expected: `OK`.

---

### Task 5: The manifest

**Files:**
- Create: `ghost_update/manifest.py`, `tests/test_manifest.py`

- [ ] **Step 1: Write the failing test,** `tests/test_manifest.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path

from ghost_update import manifest
from ghost_update.identity import BROWSER_APPID, UPDATER_APPID
from ghost_update.protocol import Release


def release(version: str, directory: Path, size: int = 10) -> Release:
    name = f"browser-{version}.crx3"
    (directory / name).write_bytes(b"x" * size)
    return Release(version, name, size, "ab" * 32, "mini_installer.exe", "--do-not-launch-chrome")


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)

    def write(self, doc) -> None:
        (self.dir / manifest.FILE_NAME).write_text(json.dumps(doc))

    def test_round_trip(self):
        current = release("152.0.7977.14902", self.dir)
        older = release("152.0.7977.14901", self.dir)
        original = manifest.Manifest({BROWSER_APPID: manifest.AppEntry(current, (older,)),
                                      UPDATER_APPID: manifest.AppEntry(None, ())})
        manifest.write(original, self.dir)
        parsed = manifest.parse((self.dir / manifest.FILE_NAME).read_bytes(), self.dir)
        self.assertEqual(parsed, original)
        self.assertEqual(parsed.releases(), {BROWSER_APPID: current, UPDATER_APPID: None})
        self.assertFalse((self.dir / (manifest.FILE_NAME + ".new")).exists())

    def test_invalid_manifests(self):
        good = {"version": "152.0.7977.14902", "file": "browser-152.0.7977.14902.crx3",
                "size": 10, "sha256": "ab" * 32, "installer": "mini_installer.exe",
                "arguments": ""}
        (self.dir / good["file"]).write_bytes(b"x" * 10)
        cases = {
            "not JSON": b"{",
            "no apps": json.dumps({"apps": {}}).encode(),
            "uppercase app ID": json.dumps({"apps": {BROWSER_APPID.upper(): {
                "active": None, "previous": []}}}).encode(),
            "missing previous": json.dumps({"apps": {BROWSER_APPID: {
                "active": None}}}).encode(),
            "too many kept": json.dumps({"apps": {BROWSER_APPID: {
                "active": good, "previous": [good, good, good]}}}).encode(),
        }
        for field, value in (("version", "1.2.3"), ("file", "../etc/passwd"),
                             ("file", "browser.exe"), ("size", 11), ("size", True),
                             ("sha256", "AB" * 32), ("installer", ""), ("arguments", None)):
            cases[f"{field}={value!r}"] = json.dumps({"apps": {BROWSER_APPID: {
                "active": {**good, field: value}, "previous": []}}}).encode()
        cases["extra field"] = json.dumps({"apps": {BROWSER_APPID: {
            "active": {**good, "url": "x"}, "previous": []}}}).encode()
        for name, data in cases.items():
            with self.subTest(name):
                with self.assertRaises(manifest.ManifestError):
                    manifest.parse(data, self.dir)

    def test_a_missing_package_is_invalid(self):
        current = release("152.0.7977.14902", self.dir)
        manifest.write(manifest.Manifest({BROWSER_APPID: manifest.AppEntry(current, ())}),
                       self.dir)
        (self.dir / current.file).unlink()
        with self.assertRaises(manifest.ManifestError):
            manifest.parse((self.dir / manifest.FILE_NAME).read_bytes(), self.dir)


class StoreTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        self.first = release("152.0.7977.14901", self.dir)
        manifest.write(manifest.Manifest({BROWSER_APPID: manifest.AppEntry(self.first, ())}),
                       self.dir)
        self.store = manifest.Store(self.dir)

    def touch(self, seconds: int) -> None:
        path = self.dir / manifest.FILE_NAME
        mtime = path.stat().st_mtime_ns + seconds * 1_000_000_000
        os.utime(path, ns=(mtime, mtime))

    def test_reloads_when_the_file_changes(self):
        second = release("152.0.7977.14902", self.dir)
        manifest.write(manifest.Manifest({BROWSER_APPID: manifest.AppEntry(second, ())}),
                       self.dir)
        self.touch(5)
        self.assertEqual(self.store.current().releases()[BROWSER_APPID], second)

    def test_keeps_the_last_good_manifest(self):
        (self.dir / manifest.FILE_NAME).write_text("{")
        self.touch(5)
        with self.assertLogs("ghost-update", "ERROR") as logs:
            self.assertEqual(self.store.current().releases()[BROWSER_APPID], self.first)
        self.assertIn("keeping the last good one", logs.output[0])
        with self.assertNoLogs("ghost-update", "ERROR"):
            self.store.current()  # the same bad file is reported once

    def test_a_missing_manifest_keeps_the_last_good_one(self):
        (self.dir / manifest.FILE_NAME).unlink()
        with self.assertLogs("ghost-update", "ERROR"):
            self.assertEqual(self.store.current().releases()[BROWSER_APPID], self.first)

    def test_the_store_needs_a_valid_manifest_to_start(self):
        (self.dir / manifest.FILE_NAME).write_text("{")
        with self.assertRaises(manifest.ManifestError):
            manifest.Store(self.dir)
        (self.dir / manifest.FILE_NAME).unlink()
        with self.assertRaises(OSError):
            manifest.Store(self.dir)


if __name__ == "__main__":
    unittest.main()
```

Run: `python -m unittest tests.test_manifest -v`. Expected: `ImportError: cannot import name 'manifest'`.

- [ ] **Step 2: Write `ghost_update/manifest.py`.**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""releases.json: the app IDs the server knows and each one's active release.

    {"apps": {"{app id}": {"active": <release> | null, "previous": [<release>, ...]}}}

A release is {"version", "file", "size", "sha256", "installer", "arguments"},
its file in the releases directory. `previous` lists up to two earlier
releases whose files stay on disk. This file is the server's whole state.
"""

from __future__ import annotations

import json
import logging
import os
import re
import threading
from dataclasses import asdict, dataclass
from pathlib import Path

from ghost_update.protocol import InvalidRequest, Release, parse_version

FILE_NAME = "releases.json"
KEPT = 3  # the active release and two previous ones
_APPID = re.compile(r"^\{[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\}$")
_FILE = re.compile(r"^[A-Za-z0-9._-]+\.crx3$")
_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_RELEASE_FIELDS = {"version", "file", "size", "sha256", "installer", "arguments"}
log = logging.getLogger("ghost-update")


class ManifestError(ValueError):
    pass


@dataclass(frozen=True)
class AppEntry:
    active: Release | None
    previous: tuple[Release, ...]


@dataclass(frozen=True)
class Manifest:
    apps: dict[str, AppEntry]  # lowercase app ID -> entry

    def releases(self) -> dict[str, Release | None]:
        return {appid: entry.active for appid, entry in self.apps.items()}


def _release(value: object, releases_dir: Path) -> Release:
    if not isinstance(value, dict) or set(value) != _RELEASE_FIELDS:
        raise ManifestError("a release has exactly: " + ", ".join(sorted(_RELEASE_FIELDS)))
    try:
        parse_version(value["version"])
    except InvalidRequest:
        raise ManifestError(f"bad version {value['version']!r}") from None
    if not isinstance(value["file"], str) or not _FILE.match(value["file"]):
        raise ManifestError(f"bad file name {value['file']!r}")
    size = value["size"]
    if isinstance(size, bool) or not isinstance(size, int) or size <= 0:
        raise ManifestError(f"bad size {size!r}")
    if not isinstance(value["sha256"], str) or not _SHA256.match(value["sha256"]):
        raise ManifestError("bad sha256")
    if not isinstance(value["installer"], str) or not value["installer"]:
        raise ManifestError("bad installer")
    if not isinstance(value["arguments"], str):
        raise ManifestError("bad arguments")
    path = releases_dir / value["file"]
    if not path.is_file() or path.stat().st_size != size:
        raise ManifestError(f"{value['file']} is missing or not {size} bytes")
    return Release(**value)


def parse(data: bytes, releases_dir: Path) -> Manifest:
    try:
        doc = json.loads(data)
    except (ValueError, UnicodeDecodeError):
        raise ManifestError("not JSON") from None
    apps = doc.get("apps") if isinstance(doc, dict) else None
    if not isinstance(apps, dict) or not apps:
        raise ManifestError("no apps")
    out = {}
    for appid, entry in apps.items():
        if not _APPID.match(appid):
            raise ManifestError(f"bad app ID {appid!r}: lowercase, in braces")
        if not isinstance(entry, dict) or set(entry) != {"active", "previous"}:
            raise ManifestError(f"{appid}: needs active and previous")
        previous = entry["previous"]
        if not isinstance(previous, list) or len(previous) > KEPT - 1:
            raise ManifestError(f"{appid}: previous is a list of at most {KEPT - 1}")
        active = None if entry["active"] is None else _release(entry["active"], releases_dir)
        out[appid] = AppEntry(active, tuple(_release(p, releases_dir) for p in previous))
    return Manifest(out)


def serialize(manifest: Manifest) -> bytes:
    doc = {"apps": {appid: {"active": asdict(entry.active) if entry.active else None,
                            "previous": [asdict(p) for p in entry.previous]}
                    for appid, entry in sorted(manifest.apps.items())}}
    return (json.dumps(doc, indent=2) + "\n").encode()


def write(manifest: Manifest, releases_dir: Path) -> None:
    """Writes releases.json atomically: a reader sees the old file or the new one."""
    path = releases_dir / FILE_NAME
    temporary = path.with_name(FILE_NAME + ".new")
    with open(temporary, "wb") as f:
        f.write(serialize(manifest))
        f.flush()
        os.fsync(f.fileno())
    os.replace(temporary, path)


class Store:
    """The active manifest, reloaded when releases.json changes. A new file
    that is missing or invalid is logged once, and the last good one stays."""

    def __init__(self, releases_dir: Path):
        self._dir = releases_dir
        self._path = releases_dir / FILE_NAME
        self._lock = threading.Lock()
        self._seen = self._path.stat().st_mtime_ns  # raises if missing
        self._manifest = parse(self._path.read_bytes(), releases_dir)

    def current(self) -> Manifest:
        with self._lock:
            try:
                mtime = self._path.stat().st_mtime_ns
            except OSError:
                mtime = -1
            if mtime != self._seen:
                self._seen = mtime
                try:
                    self._manifest = parse(self._path.read_bytes(), self._dir)
                    log.info("releases.json reloaded")
                except (OSError, ManifestError) as e:
                    log.error("releases.json not reloaded, keeping the last good one: %s", e)
            return self._manifest
```

- [ ] **Step 3: Run the tests; they pass. Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest tests.test_manifest -v 2>&1 | tail -3
git add -A && python tools/lint.py && git commit -q -s -m "manifest: releases.json, validated and written atomically

Each release names a package that must exist at its size. The store
reloads the file when it changes; a missing or invalid new file is
logged once and the last good manifest stays active."
```

Expected: `OK`.

---

### Task 6: The service

**Files:**
- Create: `ghost_update/service.py`, `tests/test_service.py`

- [ ] **Step 1: Write the failing test,** `tests/test_service.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import contextlib
import hashlib
import http.client
import io
import json
import logging
import os
import shutil
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from ghost_update import cup, manifest, service
from ghost_update.identity import BROWSER_APPID, UPDATER_APPID
from ghost_update.protocol import Release
from tests.helpers import CUP_KEY_FILE, reference_verify

SESSION_MARKER = "{5e55104e-0000-4000-8000-00000000f00d}"
REQUEST_MARKER = "{12e90e57-0000-4000-8000-00000000beef}"


def request_body(version: str) -> bytes:
    return json.dumps({"request": {
        "protocol": "4.0", "sessionid": SESSION_MARKER, "requestid": REQUEST_MARKER,
        "apps": [{"appid": BROWSER_APPID, "version": version, "updatecheck": {}}]}}).encode()


class ServiceTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        self.publish("152.0.7977.14902")
        self.key = cup.load_key(CUP_KEY_FILE)
        numbers = self.key.public_key().public_numbers()
        self.public = (numbers.x, numbers.y)
        self.service = service.Service(0, manifest.Store(self.dir), self.key,
                                       "https://203.0.113.5")
        threading.Thread(target=self.service.serve_forever, daemon=True).start()
        self.addCleanup(self.service.server_close)
        self.addCleanup(self.service.shutdown)

    def publish(self, version: str) -> None:
        name = f"browser-{version}.crx3"
        (self.dir / name).write_bytes(b"x" * 100)
        release = Release(version, name, 100, hashlib.sha256(b"x" * 100).hexdigest(),
                          "mini_installer.exe", "--do-not-launch-chrome")
        manifest.write(manifest.Manifest({BROWSER_APPID: manifest.AppEntry(release, ()),
                                          UPDATER_APPID: manifest.AppEntry(None, ())}),
                       self.dir)

    def post(self, body: bytes, query: str = "cup2key=1:12345", path: str = "/update"):
        conn = http.client.HTTPConnection("127.0.0.1", self.service.server_address[1],
                                          timeout=10)
        conn.request("POST", f"{path}?{query}" if query else path, body=body)
        response = conn.getresponse()
        data = response.read()
        conn.close()
        return response.status, response.getheader("X-Cup-Server-Proof"), data

    def test_an_update_is_offered_and_signed(self):
        body = request_body("152.0.7977.14901")
        status, proof, data = self.post(body)
        self.assertEqual(status, 200)
        self.assertTrue(reference_verify(self.public, "1:12345", body, data, proof))
        [app] = json.loads(data[5:])["response"]["apps"]
        self.assertEqual(app["updatecheck"]["pipelines"][0]["operations"][0]["urls"],
                         [{"url": "https://203.0.113.5/releases/browser-152.0.7977.14902.crx3"}])

    def test_refusals(self):
        cases = {
            "no cup2key": (request_body("1.0.0.0"), "", "/update", 400),
            "another key version": (request_body("1.0.0.0"), "cup2key=2:12345", "/update", 400),
            "invalid request": (b"{", "cup2key=1:12345", "/update", 400),
            "another path": (request_body("1.0.0.0"), "cup2key=1:12345", "/other", 404),
        }
        for name, (body, query, path, expected) in cases.items():
            with self.subTest(name):
                status, proof, data = self.post(body, query, path)
                self.assertEqual((status, proof, data), (expected, None, b""))

    def test_an_oversized_body_is_refused_unread(self):
        conn = http.client.HTTPConnection("127.0.0.1", self.service.server_address[1],
                                          timeout=10)
        conn.putrequest("POST", "/update?cup2key=1:12345")
        conn.putheader("Content-Length", str(service.MAX_BODY + 1))
        conn.endheaders()
        self.assertEqual(conn.getresponse().status, 413)
        conn.close()

    def test_get_is_not_served(self):
        conn = http.client.HTTPConnection("127.0.0.1", self.service.server_address[1],
                                          timeout=10)
        conn.request("GET", "/update")
        self.assertEqual(conn.getresponse().status, 404)
        conn.close()

    def test_a_new_release_is_picked_up(self):
        self.publish("152.0.7977.14903")
        path = self.dir / manifest.FILE_NAME
        mtime = path.stat().st_mtime_ns + 5_000_000_000
        os.utime(path, ns=(mtime, mtime))
        _, _, data = self.post(request_body("152.0.7977.14901"))
        [app] = json.loads(data[5:])["response"]["apps"]
        self.assertEqual(app["updatecheck"]["nextversion"], "152.0.7977.14903")

    def test_nothing_from_a_request_is_written(self):
        stream = io.StringIO()
        handler = logging.StreamHandler(stream)
        logger = logging.getLogger("ghost-update")
        logger.addHandler(handler)
        logger.setLevel(logging.DEBUG)
        self.addCleanup(logger.setLevel, logging.NOTSET)
        self.addCleanup(logger.removeHandler, handler)
        with contextlib.redirect_stderr(stream):
            self.post(request_body("152.0.7977.14901"))
            self.post(request_body("9.9.9"))                     # an invalid version
            self.post(request_body("152.0.7977.14901"), query="")  # no cup2key
        written = stream.getvalue()
        self.assertIn("refused", written)  # the capture works
        self.assertNotIn(SESSION_MARKER, written)
        self.assertNotIn(REQUEST_MARKER, written)
        self.assertNotIn("127.0.0.1", written)


class MainTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        self.config = self.dir / "server.json"
        self.config.write_text(json.dumps({"port": 0, "public_url": "https://203.0.113.5",
                                           "releases_dir": str(self.dir)}))

    def test_no_key_no_start(self):
        manifest.write(manifest.Manifest({UPDATER_APPID: manifest.AppEntry(None, ())}),
                       self.dir)
        with mock.patch.dict(os.environ):
            os.environ.pop("CREDENTIALS_DIRECTORY", None)
            with self.assertLogs("ghost-update", "ERROR"):
                self.assertEqual(service.main(["--config", str(self.config)]), 1)

    def test_no_valid_manifest_no_start(self):
        (self.dir / manifest.FILE_NAME).write_text("{")
        with self.assertLogs("ghost-update", "ERROR"):
            self.assertEqual(service.main(["--config", str(self.config),
                                           "--key", str(CUP_KEY_FILE)]), 1)


if __name__ == "__main__":
    unittest.main()
```

Run: `python -m unittest tests.test_service -v`. Expected: `ImportError: cannot import name 'service'`.

- [ ] **Step 2: Write `ghost_update/service.py`.**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The update service: answers POST /update on loopback, behind Caddy.

    python3 -m ghost_update.service --config /etc/ghost-update/server.json [--key FILE]

The CUP key defaults to systemd's credential "cup_key" ($CREDENTIALS_DIRECTORY).
The service never sees a client's address, since Caddy proxies every request
from loopback, and it writes nothing that comes from a request.
"""

from __future__ import annotations

import argparse
import datetime
import http.server
import json
import logging
import os
import sys
import urllib.parse
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric import ec

from ghost_update import cup, manifest, protocol

MAX_BODY = 64 * 1024
log = logging.getLogger("ghost-update")


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    timeout = 10
    server: Service

    def do_POST(self) -> None:
        url = urllib.parse.urlsplit(self.path)
        if url.path != "/update":
            self._send(404)
            return
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            self._refuse(400, "a request without a length")
            return
        if not 0 <= length <= MAX_BODY:
            self._refuse(413, "a request body over 64 KiB")
            return
        body = self.rfile.read(length)
        cup2key = urllib.parse.parse_qs(url.query).get("cup2key", [""])[0]
        if not cup.valid_cup2key(cup2key):
            self._refuse(400, "a request without a valid cup2key")
            return
        try:
            payload = protocol.respond(body, self.server.store.current().releases(),
                                       self.server.download_base, datetime.date.today())
        except protocol.InvalidRequest as e:
            self._refuse(400, f"an invalid request: {e}")
            return
        self._send(200, payload, {
            "Content-Type": "application/json",
            "X-Cup-Server-Proof": cup.proof(self.server.key, cup2key, body, payload)})

    def do_GET(self) -> None:
        self._send(404)

    def _refuse(self, status: int, reason: str) -> None:
        log.warning("refused %s", reason)
        self.close_connection = True
        self._send(status)

    def _send(self, status: int, body: bytes = b"", headers: dict | None = None) -> None:
        self.send_response(status)
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args) -> None:
        pass  # the default writes the client's address and the request line


class Service(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, port: int, store: manifest.Store, key: ec.EllipticCurvePrivateKey,
                 public_url: str):
        super().__init__(("127.0.0.1", port), Handler)
        self.store, self.key = store, key
        self.download_base = public_url.rstrip("/") + "/releases"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--key", type=Path, help="the CUP key file (tests and development)")
    args = parser.parse_args(argv)
    if not log.handlers:
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(logging.Formatter("%(levelname)s %(message)s"))
        log.addHandler(handler)
        log.setLevel(logging.INFO)
    key_path = args.key
    if key_path is None and "CREDENTIALS_DIRECTORY" in os.environ:
        key_path = Path(os.environ["CREDENTIALS_DIRECTORY"]) / "cup_key"
    if key_path is None:
        log.error("not starting: no CUP key (--key, or systemd's LoadCredential=cup_key)")
        return 1
    try:
        config = json.loads(args.config.read_text(encoding="utf-8"))
        key = cup.load_key(key_path)
        store = manifest.Store(Path(config["releases_dir"]))
        server = Service(int(config["port"]), store, key, config["public_url"])
    except (OSError, ValueError, KeyError) as e:
        log.error("not starting: %s", e)
        return 1
    log.info("serving on 127.0.0.1:%d", server.server_address[1])
    server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

`ManifestError` is a `ValueError`, so the `except` covers an invalid manifest.

- [ ] **Step 3: Run the tests; they pass. Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest tests.test_service -v 2>&1 | tail -3
git add -A && python tools/lint.py && git commit -q -s -m "service: POST /update on loopback, signed, writing nothing from requests

Every answer carries a CUP proof; a request without a valid cup2key, an
invalid request or a body over 64 KiB is refused without a body. The
default request log, which names the client, is off, and a test checks
that markers sent in requests never reach the output."
```

Expected: `OK`.

---

### Task 7: `ghost-update-admin`

**Files:**
- Create: `ghost_update/admin.py`, `tests/test_admin.py`

- [ ] **Step 1: Write the failing test,** `tests/test_admin.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import contextlib
import hashlib
import io
import json
import shutil
import tempfile
import unittest
from pathlib import Path

from ghost_update import admin, manifest
from ghost_update.identity import BROWSER_APPID, UPDATER_APPID


class AdminTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        (self.dir / "staging").mkdir()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(admin.main(["--releases-dir", str(self.dir), "init"]), 0)

    def activate(self, version: str, content: bytes = b"package") -> int:
        staged = self.dir / "staging" / f"browser-{version}.crx3"
        staged.write_bytes(content)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return admin.main(["--releases-dir", str(self.dir), "activate", "--staged",
                               str(staged), "--appid", BROWSER_APPID.upper(),
                               "--version", version])

    def current(self) -> manifest.Manifest:
        return manifest.parse((self.dir / manifest.FILE_NAME).read_bytes(), self.dir)

    def test_init_creates_ghost_apps_once(self):
        self.assertEqual(self.current().releases(), {BROWSER_APPID: None, UPDATER_APPID: None})
        before = (self.dir / manifest.FILE_NAME).read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(admin.main(["--releases-dir", str(self.dir), "init"]), 0)
        self.assertEqual((self.dir / manifest.FILE_NAME).read_bytes(), before)

    def test_activate(self):
        self.assertEqual(self.activate("152.0.7977.14902"), 0)
        active = self.current().apps[BROWSER_APPID].active
        self.assertEqual(active, manifest.Release(
            "152.0.7977.14902", "c0ff4371-d9ab-461e-bffd-6b0dc2430b02-152.0.7977.14902.crx3",
            7, hashlib.sha256(b"package").hexdigest(), admin.DEFAULT_INSTALLER,
            admin.DEFAULT_ARGUMENTS))
        self.assertTrue((self.dir / active.file).is_file())
        self.assertFalse(any((self.dir / "staging").iterdir()))

    def test_activate_refuses_a_version_not_newer(self):
        self.assertEqual(self.activate("152.0.7977.14902"), 0)
        for version in ("152.0.7977.14902", "152.0.7977.14901"):
            with self.subTest(version):
                self.assertEqual(self.activate(version), 1)
                self.assertTrue((self.dir / "staging" / f"browser-{version}.crx3").exists())
        self.assertEqual(self.current().apps[BROWSER_APPID].active.version, "152.0.7977.14902")

    def test_activate_refuses_an_unknown_app_and_a_bad_version(self):
        staged = self.dir / "staging" / "x.crx3"
        staged.write_bytes(b"x")
        for appid, version in (("{00000000-0000-0000-0000-000000000000}", "1.0.0.0"),
                               (BROWSER_APPID, "1.0.0")):
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(admin.main(["--releases-dir", str(self.dir), "activate",
                                             "--staged", str(staged), "--appid", appid,
                                             "--version", version]), 1)

    def test_three_releases_are_kept(self):
        for respin in range(1, 5):
            self.assertEqual(self.activate(f"152.0.7977.1490{respin}"), 0)
        entry = self.current().apps[BROWSER_APPID]
        self.assertEqual([entry.active.version] + [p.version for p in entry.previous],
                         ["152.0.7977.14904", "152.0.7977.14903", "152.0.7977.14902"])
        self.assertEqual(sorted(p.name for p in self.dir.glob("*.crx3")), sorted(
            f"c0ff4371-d9ab-461e-bffd-6b0dc2430b02-152.0.7977.1490{r}.crx3" for r in (2, 3, 4)))

    def test_list(self):
        self.activate("152.0.7977.14902")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(admin.main(["--releases-dir", str(self.dir), "list"]), 0)
        self.assertIn(f"{BROWSER_APPID}  active 152.0.7977.14902  kept none", out.getvalue())
        self.assertIn(f"{UPDATER_APPID}  active none  kept none", out.getvalue())


class FindAddressTest(unittest.TestCase):
    ADDRESS = "198.51.100.7"

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root)
        (self.root / "log").mkdir()
        (self.root / "log" / "apt.log").write_text("installed caddy\n")

    def find(self, entries):
        return admin.find_address(self.ADDRESS, roots=(self.root,), entries=entries)

    def test_nothing_found(self):
        self.assertEqual(self.find([{"_COMM": "caddy", "MESSAGE": "serving"}]), [])

    def test_administrators_records_are_skipped(self):
        message = f"Accepted publickey for ghost from {self.ADDRESS} port 51234"
        (self.root / "log" / "wtmp").write_bytes(self.ADDRESS.encode())
        self.assertEqual(self.find([{"_COMM": "sshd", "MESSAGE": message},
                                    {"_COMM": "sudo", "MESSAGE": f"COMMAND=x {self.ADDRESS}"},
                                    {"SYSLOG_IDENTIFIER": "sshd-session", "MESSAGE": message}]),
                         [])

    def test_the_journal_is_searched(self):
        found = self.find([{"_COMM": "caddy", "_SYSTEMD_UNIT": "caddy.service",
                            "MESSAGE": f"TLS handshake error from {self.ADDRESS}:51234"}])
        self.assertEqual(found, ["journal: caddy.service"])

    def test_files_are_searched(self):
        (self.root / "log" / "caddy").mkdir()
        (self.root / "log" / "caddy" / "access.log").write_text(json.dumps(
            {"request": {"remote_ip": self.ADDRESS}}))
        self.assertEqual(self.find([]), [f"file: {self.root / 'log' / 'caddy' / 'access.log'}"])

    def test_the_output_never_names_the_address(self):
        (self.root / "log" / "x.log").write_text(self.ADDRESS)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = admin.report(self.find([]))
        self.assertEqual(code, 1)
        self.assertNotIn(self.ADDRESS, out.getvalue())


if __name__ == "__main__":
    unittest.main()
```

Run: `python -m unittest tests.test_admin -v`. Expected: `ImportError: cannot import name 'admin'`.

- [ ] **Step 2: Write `ghost_update/admin.py`.**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Administers the server's releases; /usr/local/sbin/ghost-update-admin.

  init                       create releases.json with Ghost's app IDs, if missing
  activate --staged F --appid A --version V [--installer I] [--arguments ARGS]
                             make a staged package the app's active release
  list                       each app's active and kept releases
  find-address ADDRESS       search the server's logs and data for an address;
                             exits 1 if found, without printing it
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from collections.abc import Iterable
from pathlib import Path

from ghost_update import identity, manifest
from ghost_update.protocol import InvalidRequest, Release, parse_version

DEFAULT_DIR = Path("/srv/releases")
DEFAULT_INSTALLER = "mini_installer.exe"
DEFAULT_ARGUMENTS = "--verbose-logging --do-not-launch-chrome"
SEARCH_ROOTS = (Path("/var/log"), Path("/var/lib/caddy"), Path("/srv"))
# Records of the administrators' own logins and commands, not of browser users.
ADMIN_PROGRAMS = {"sshd", "sshd-session", "sudo", "systemd-logind"}
ADMIN_FILES = {"wtmp", "btmp", "lastlog"}


def _error(message: str) -> int:
    print(f"ghost-update-admin: {message}", file=sys.stderr)
    return 1


def init(releases_dir: Path) -> int:
    if (releases_dir / manifest.FILE_NAME).exists():
        print("releases.json exists; unchanged")
        return 0
    manifest.write(manifest.Manifest({
        identity.BROWSER_APPID: manifest.AppEntry(None, ()),
        identity.UPDATER_APPID: manifest.AppEntry(None, ())}), releases_dir)
    print("releases.json created")
    return 0


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def activate(releases_dir: Path, staged: Path, appid: str, version: str, installer: str,
             arguments: str) -> int:
    appid = appid.lower()
    current = manifest.parse((releases_dir / manifest.FILE_NAME).read_bytes(), releases_dir)
    if appid not in current.apps:
        return _error(f"unknown app {appid}")
    try:
        new_version = parse_version(version)
    except InvalidRequest:
        return _error("the version is not four dotted integers")
    entry = current.apps[appid]
    if entry.active and new_version <= parse_version(entry.active.version):
        return _error(f"{version} is not newer than the active {entry.active.version}")
    name = f"{appid.strip('{}')}-{version}.crx3"
    release = Release(version, name, staged.stat().st_size, _sha256(staged), installer,
                      arguments)
    os.replace(staged, releases_dir / name)
    kept = ((entry.active,) if entry.active else ()) + entry.previous
    updated = manifest.Manifest({**current.apps, appid: manifest.AppEntry(
        release, kept[:manifest.KEPT - 1])})
    manifest.parse(manifest.serialize(updated), releases_dir)  # validate before writing
    manifest.write(updated, releases_dir)
    for old in kept[manifest.KEPT - 1:]:
        (releases_dir / old.file).unlink(missing_ok=True)
    print(f"{appid} {version} active")
    return 0


def list_releases(releases_dir: Path) -> int:
    current = manifest.parse((releases_dir / manifest.FILE_NAME).read_bytes(), releases_dir)
    for appid, entry in sorted(current.apps.items()):
        active = entry.active.version if entry.active else "none"
        kept = ", ".join(p.version for p in entry.previous) or "none"
        print(f"{appid}  active {active}  kept {kept}")
    return 0


def journal() -> Iterable[dict]:
    out = subprocess.run(["journalctl", "-o", "json", "--no-pager"], capture_output=True,
                         text=True, check=True).stdout
    for line in out.splitlines():
        try:
            yield json.loads(line)
        except ValueError:
            continue


def find_address(address: str, roots: Iterable[Path] = SEARCH_ROOTS,
                 entries: Iterable[dict] | None = None) -> list[str]:
    """Where the address appears, outside the administrators' own records."""
    found = []
    for entry in journal() if entries is None else entries:
        if {entry.get("_COMM"), entry.get("SYSLOG_IDENTIFIER")} & ADMIN_PROGRAMS:
            continue
        if address in json.dumps(entry):
            found.append("journal: " + str(entry.get("_SYSTEMD_UNIT")
                                           or entry.get("SYSLOG_IDENTIFIER") or "?"))
    needle = address.encode()
    for root in roots:
        for path in sorted(root.rglob("*")) if root.is_dir() else ():
            if (path.name in ADMIN_FILES or "journal" in path.parts or path.is_symlink()
                    or not path.is_file()):
                continue
            try:
                if needle in path.read_bytes():
                    found.append(f"file: {path}")
            except OSError:
                continue
    return sorted(set(found))


def report(found: list[str]) -> int:
    if not found:
        print("no record of the address")
        return 0
    for place in found:
        print(f"found in {place}")
    return 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="ghost-update-admin",
                                     description=__doc__.splitlines()[0])
    parser.add_argument("--releases-dir", type=Path, default=DEFAULT_DIR)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("init")
    a = sub.add_parser("activate")
    a.add_argument("--staged", type=Path, required=True)
    a.add_argument("--appid", required=True)
    a.add_argument("--version", required=True)
    a.add_argument("--installer", default=DEFAULT_INSTALLER)
    a.add_argument("--arguments", default=DEFAULT_ARGUMENTS)
    sub.add_parser("list")
    f = sub.add_parser("find-address")
    f.add_argument("address")
    args = parser.parse_args(argv)
    if args.command == "init":
        return init(args.releases_dir)
    if args.command == "activate":
        return activate(args.releases_dir, args.staged, args.appid, args.version,
                        args.installer, args.arguments)
    if args.command == "list":
        return list_releases(args.releases_dir)
    return report(find_address(args.address))


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Run the tests; they pass. Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest tests.test_admin -v 2>&1 | tail -3
git add -A && python tools/lint.py && git commit -q -s -m "admin: activate releases, list them, search for an address

activate refuses a version that isn't newer, moves the staged package
into place, keeps three releases and writes the manifest atomically.
find-address searches the journal and the server's files, skipping the
administrators' own login records, and never prints the address."
```

Expected: `OK`.

---

### Task 8: The release CLI

**Files:**
- Create: `ghost_update/release.py`, `tests/test_release.py`

- [ ] **Step 1: Write the failing test,** `tests/test_release.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import unittest
from pathlib import Path

from ghost_update import release
from ghost_update.identity import BROWSER_APPID
from ghost_update.reference import crx3
from tests.helpers import CRX_KEY_FILE, OTHER_KEY, private_key


class CheckPackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = {"mini_installer.exe": b"MZ installer"}
        cls.good = crx3.build(cls.files, private_key(CRX_KEY_FILE))

    def test_a_package_by_ghosts_publisher_key_passes(self):
        release.check_package(self.good, "mini_installer.exe")

    def test_refusals(self):
        cases = {
            "another key": (crx3.build(self.files, OTHER_KEY), "mini_installer.exe"),
            "no installer": (self.good, "setup.exe"),
            "not a CRX3": (b"PK\x03\x04", "mini_installer.exe"),
            "truncated": (self.good[:40], "mini_installer.exe"),
            "a changed archive": (self.good[:-1] + bytes([self.good[-1] ^ 1]),
                                  "mini_installer.exe"),
        }
        for name, (data, installer) in cases.items():
            with self.subTest(name):
                with self.assertRaises(release.ReleaseError):
                    release.check_package(data, installer)


class CommandsTest(unittest.TestCase):
    def test_upload_then_activate(self):
        staged = "/srv/releases/staging/c0ff4371-d9ab-461e-bffd-6b0dc2430b02-152.0.7977.14902.crx3"
        self.assertEqual(
            release.commands(Path("update.crx3"), BROWSER_APPID, "152.0.7977.14902",
                             "ghost@203.0.113.5", "mini_installer.exe",
                             "--verbose-logging --do-not-launch-chrome"),
            [["scp", "-q", "update.crx3", f"ghost@203.0.113.5:{staged}"],
             ["ssh", "ghost@203.0.113.5",
              f"sudo ghost-update-admin activate --staged {staged} --appid "
              "'{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}' --version 152.0.7977.14902 "
              "--installer mini_installer.exe "
              "--arguments '--verbose-logging --do-not-launch-chrome'"]])

    def test_a_bad_version_or_app_id_is_refused(self):
        for appid, version in ((BROWSER_APPID, "1.2.3"), ("{x}", "1.2.3.4"),
                               (BROWSER_APPID + ";rm", "1.2.3.4")):
            with self.subTest((appid, version)):
                with self.assertRaises(release.ReleaseError):
                    release.commands(Path("u.crx3"), appid, version, "ghost@h",
                                     "mini_installer.exe", "")


if __name__ == "__main__":
    unittest.main()
```

Run: `python -m unittest tests.test_release -v`. Expected: `ImportError: cannot import name 'release'`.

- [ ] **Step 2: Write `ghost_update/release.py`.**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Publishes a release: checks a CRX3 here, uploads it, activates it.

    python -m ghost_update.release --crx F --appid A --version V --host USER@HOST
           [--installer mini_installer.exe] [--arguments ARGS]

Runs on the build machine with the system's ssh and scp. The package must
carry a valid proof by Ghost's publisher key and contain its installer; the
server's ghost-update-admin then refuses a version that isn't newer.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import re
import shlex
import subprocess
import sys
import zipfile
from pathlib import Path

from ghost_update import admin, identity
from ghost_update.protocol import InvalidRequest, parse_version
from ghost_update.reference import crx3

STAGING = "/srv/releases/staging"
_APPID = re.compile(r"^\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
                    r"[0-9a-fA-F]{12}\}$")


class ReleaseError(ValueError):
    pass


def check_package(data: bytes, installer: str,
                  publisher_key_sha256: str = identity.PUBLISHER_KEY_SHA256) -> None:
    try:
        keys = crx3.verified_keys(data)
        archive = crx3.parse(data).archive
    except (ValueError, IndexError):
        raise ReleaseError("not a CRX3 package") from None
    if not any(hashlib.sha256(key).hexdigest() == publisher_key_sha256 for key in keys):
        raise ReleaseError("no valid proof by Ghost's publisher key")
    try:
        names = zipfile.ZipFile(io.BytesIO(archive)).namelist()
    except zipfile.BadZipFile:
        raise ReleaseError("the package holds no zip archive") from None
    if installer not in names:
        raise ReleaseError(f"the package doesn't contain {installer}")


def commands(crx: Path, appid: str, version: str, host: str, installer: str,
             arguments: str) -> list[list[str]]:
    if not _APPID.match(appid):
        raise ReleaseError("the app ID is not a GUID in braces")
    try:
        parse_version(version)
    except InvalidRequest:
        raise ReleaseError("the version is not four dotted integers") from None
    staged = f"{STAGING}/{appid.strip('{}').lower()}-{version}.crx3"
    activate = ["sudo", "ghost-update-admin", "activate", "--staged", staged, "--appid", appid,
                "--version", version, "--installer", installer, "--arguments", arguments]
    return [["scp", "-q", str(crx), f"{host}:{staged}"], ["ssh", host, shlex.join(activate)]]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--crx", type=Path, required=True)
    parser.add_argument("--appid", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--host", required=True, help="USER@HOST of the server")
    parser.add_argument("--installer", default=admin.DEFAULT_INSTALLER)
    parser.add_argument("--arguments", default=admin.DEFAULT_ARGUMENTS)
    args = parser.parse_args(argv)
    try:
        steps = commands(args.crx, args.appid, args.version, args.host, args.installer,
                         args.arguments)
        check_package(args.crx.read_bytes(), args.installer)
    except (OSError, ReleaseError) as e:
        print(f"release: {e}", file=sys.stderr)
        return 1
    for step in steps:
        if subprocess.run(step).returncode:
            print(f"release: failed: {step[0]}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Run the tests; they pass. Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest tests.test_release -v 2>&1 | tail -3
git add -A && python tools/lint.py && git commit -q -s -m "release: check a package, upload it, activate it

A package needs a valid proof by Ghost's publisher key and must contain
its installer before anything is uploaded. The app ID and version are
validated, and the remote command is quoted."
```

Expected: `OK`.

---

### Task 9: Deployment files

**Files:**
- Create: `deploy/Caddyfile`, `deploy/nftables.conf`, `deploy/ghost-update.service`, `deploy/server.json`, `deploy/ghost-update-admin`, `deploy/sshd.conf`, `deploy/sudoers`, `deploy/20auto-upgrades`, `tests/test_deploy_files.py`

- [ ] **Step 1: Write the failing test,** `tests/test_deploy_files.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import re
import unittest
from pathlib import Path

DEPLOY = Path(__file__).resolve().parent.parent / "deploy"


def read(name: str) -> str:
    return (DEPLOY / name).read_text(encoding="utf-8")


class CaddyfileTest(unittest.TestCase):
    def setUp(self):
        self.text = read("Caddyfile")
        self.lines = [line.strip() for line in self.text.splitlines()
                      if line.strip() and not line.strip().startswith("#")]

    def test_no_access_log(self):
        # The only `log` is the global default logger; a site-level `log` is an access log.
        self.assertEqual([line for line in self.lines if re.match(r"^log\b", line)],
                         ["log default {"])

    def test_server_logs_carry_no_request_fields(self):
        self.assertIn("exclude http.log.access http.log.error http.handlers.reverse_proxy "
                      "http.stdlib", self.lines)

    def test_the_proxy_passes_no_client_address(self):
        for header in ("X-Forwarded-For", "X-Forwarded-Proto", "X-Forwarded-Host"):
            self.assertIn(f"header_up -{header}", self.lines)
        self.assertIn("reverse_proxy 127.0.0.1:8484 {", self.lines)

    def test_only_packages_are_served(self):
        self.assertIn(r"@package path_regexp ^/[A-Za-z0-9._-]+\.crx3$", self.lines)
        self.assertIn("file_server @package {", self.lines)

    def test_tls_and_admin(self):
        for line in ("admin off", "skip_install_trust", "tls internal", "https://@ADDRESS@ {"):
            self.assertIn(line, self.lines)


class UnitTest(unittest.TestCase):
    def test_hardening(self):
        lines = set(read("ghost-update.service").splitlines())
        for line in ("DynamicUser=yes", "ProtectSystem=strict", "ProtectHome=yes",
                     "PrivateTmp=yes", "PrivateDevices=yes", "NoNewPrivileges=yes",
                     "RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX",
                     "SystemCallFilter=@system-service", "CapabilityBoundingSet=",
                     "ReadOnlyPaths=/srv/releases",
                     "LoadCredential=cup_key:/etc/ghost-update/cup_key.json",
                     "ExecStart=/usr/bin/python3 -m ghost_update.service "
                     "--config /etc/ghost-update/server.json"):
            self.assertIn(line, lines)


class FirewallTest(unittest.TestCase):
    def test_rules(self):
        text = read("nftables.conf")
        self.assertIn("policy drop;", text)
        self.assertIn("tcp dport 22 ct state new accept", text)
        self.assertIn("tcp dport { 80, 443 } ct state new accept", text)
        self.assertEqual(text.count("timeout 60s"), 2)  # both meters forget after a minute
        rules = [line for line in text.splitlines() if not line.strip().startswith("#")]
        self.assertEqual([line for line in rules if re.search(r"\blog\b", line)], [])


class ConfigTest(unittest.TestCase):
    def test_server_json(self):
        config = json.loads(read("server.json"))
        self.assertEqual(config, {"port": 8484, "public_url": "https://@ADDRESS@",
                                  "releases_dir": "/srv/releases"})

    def test_ssh(self):
        lines = set(read("sshd.conf").splitlines())
        for line in ("PermitRootLogin no", "PasswordAuthentication no",
                     "KbdInteractiveAuthentication no", "AllowUsers ghost"):
            self.assertIn(line, lines)


if __name__ == "__main__":
    unittest.main()
```

Run: `python -m unittest tests.test_deploy_files -v`. Expected: `FileNotFoundError` for `deploy/Caddyfile`.

- [ ] **Step 2: `deploy/Caddyfile`.** Indent with tabs, as Caddy formats it.

```
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Ghost's update server. provision.sh replaces @ADDRESS@ with the server's
# public address. No site has a `log` directive, so nothing records requests;
# the server's own log drops the loggers that carry request fields, the
# remote address among them (http.stdlib reports TLS handshake errors with it).
{
	admin off
	skip_install_trust
	log default {
		output stderr
		exclude http.log.access http.log.error http.handlers.reverse_proxy http.stdlib
	}
	servers {
		timeouts {
			read_header 10s
			read_body 10s
			idle 60s
		}
	}
}

https://@ADDRESS@ {
	tls internal

	handle /update {
		reverse_proxy 127.0.0.1:8484 {
			header_up -X-Forwarded-For
			header_up -X-Forwarded-Proto
			header_up -X-Forwarded-Host
		}
	}

	handle_path /releases/* {
		route {
			@package path_regexp ^/[A-Za-z0-9._-]+\.crx3$
			file_server @package {
				root /srv/releases
			}
			respond 404
		}
	}

	handle {
		respond 404
	}
}
```

There is no write timeout: a package is hundreds of megabytes, and a write timeout would cut slow downloads. `tls internal` lasts until the domain exists; it then becomes the domain name with ACME.

- [ ] **Step 3: `deploy/nftables.conf`.**

```
#!/usr/sbin/nft -f
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Ghost's update server: SSH, HTTP (ACME, once there's a domain) and HTTPS.
# New connections to 80 and 443 are limited per source address; the meters
# live in kernel memory and forget an address after 60 seconds. Nothing logs.

flush ruleset

table inet filter {
	set web4 {
		type ipv4_addr
		flags dynamic, timeout
		timeout 60s
	}

	set web6 {
		type ipv6_addr
		flags dynamic, timeout
		timeout 60s
	}

	chain input {
		type filter hook input priority filter; policy drop;
		iif "lo" accept
		ct state established,related accept
		ct state invalid drop
		meta l4proto { icmp, ipv6-icmp } accept
		tcp dport 22 ct state new accept
		tcp dport { 80, 443 } ct state new add @web4 { ip saddr limit rate over 120/minute } drop
		tcp dport { 80, 443 } ct state new add @web6 { ip6 saddr limit rate over 120/minute } drop
		tcp dport { 80, 443 } ct state new accept
	}

	chain forward {
		type filter hook forward priority filter; policy drop;
	}

	chain output {
		type filter hook output priority filter; policy accept;
	}
}
```

- [ ] **Step 4: `deploy/ghost-update.service`.**

```
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Ghost's update service, on loopback behind Caddy. The CUP key reaches it
# as a credential: the file is root's, and the service can't read it itself.

[Unit]
Description=Ghost update service
After=network.target

[Service]
ExecStart=/usr/bin/python3 -m ghost_update.service --config /etc/ghost-update/server.json
Environment=PYTHONPATH=/opt/ghost-update
LoadCredential=cup_key:/etc/ghost-update/cup_key.json
DynamicUser=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
NoNewPrivileges=yes
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
SystemCallFilter=@system-service
SystemCallArchitectures=native
CapabilityBoundingSet=
LockPersonality=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
RestrictNamespaces=yes
RestrictRealtime=yes
ReadOnlyPaths=/srv/releases
UMask=0077
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

- [ ] **Step 5: The small files.**

`deploy/server.json` (JSON has no comments; `provision.sh` replaces `@ADDRESS@`):
```json
{"port": 8484, "public_url": "https://@ADDRESS@", "releases_dir": "/srv/releases"}
```

`deploy/ghost-update-admin`:
```sh
#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
exec env PYTHONPATH=/opt/ghost-update /usr/bin/python3 -m ghost_update.admin "$@"
```

`deploy/sshd.conf`:
```
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Ghost's update server: the administrator signs in with a key; root can't.
PermitRootLogin no
PasswordAuthentication no
KbdInteractiveAuthentication no
AllowUsers ghost
```

`deploy/sudoers`:
```
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The administrator, who signs in with an SSH key only, has no password.
ghost ALL=(ALL) NOPASSWD: ALL
```

`deploy/20auto-upgrades`:
```
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
```

`server.json` can't carry the MPL notice; it isn't in lint's `HEADER_EXTENSIONS`.

- [ ] **Step 6: Run the tests and lint; they pass. Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest tests.test_deploy_files -v 2>&1 | tail -3
git add -A && python tools/lint.py && git commit -q -s -m "deploy: Caddy, nftables, systemd, SSH and sudo

Caddy keeps no access log, drops the loggers that carry request fields,
removes forwarding headers and serves only packages. The firewall limits
new connections per address in kernel memory for a minute. The service
runs as a dynamic user with systemd's hardening and gets the CUP key as
a credential. Root can't sign in."
```

Expected: `OK`, `lint: 0 problem(s)`.

---

### Task 10: `provision.sh` and `tools/deploy.py`

**Files:**
- Create: `deploy/provision.sh`, `tools/deploy.py`, `tests/test_deploy.py`

- [ ] **Step 1: Write the failing test,** `tests/test_deploy.py`:

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

from tests.helpers import CUP_KEY_FILE
from tools import deploy


class BundleTest(unittest.TestCase):
    def test_the_bundle_holds_the_package_the_deployment_and_the_key(self):
        out = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, out)
        bundle = deploy.write_bundle(out / "bundle.tar.gz", CUP_KEY_FILE)
        with tarfile.open(bundle) as archive:
            names = set(archive.getnames())
        for name in ("ghost_update/service.py", "ghost_update/reference/crx3.py",
                     "deploy/provision.sh", "deploy/Caddyfile", "cup_key.json"):
            self.assertIn(name, names)
        self.assertFalse([n for n in names if "__pycache__" in n or n.startswith("tests/")])


class CommandsTest(unittest.TestCase):
    def test_copy_then_provision(self):
        remote = deploy.REMOTE
        self.assertEqual(deploy.commands(Path("b.tar.gz"), "root@203.0.113.5", "203.0.113.5"), [
            ["scp", "-q", "b.tar.gz", f"root@203.0.113.5:{remote}.tar.gz"],
            ["ssh", "root@203.0.113.5",
             f"rm -rf {remote} && mkdir -m 700 {remote} && tar -xzf {remote}.tar.gz -C {remote}"
             f" && rm {remote}.tar.gz && sudo bash {remote}/deploy/provision.sh --address "
             f"203.0.113.5 --bundle {remote}; status=$?; rm -rf {remote}; exit $status"]])

    def test_a_bad_address_is_refused(self):
        for address in ("203.0.113.5;rm -rf /", "", "a b"):
            with self.subTest(address):
                with self.assertRaises(ValueError):
                    deploy.commands(Path("b.tar.gz"), "root@h", address)


class ProvisionScriptTest(unittest.TestCase):
    def test_syntax(self):
        bash = shutil.which("bash")
        if not bash:
            self.skipTest("no bash")
        script = Path(deploy.REPO) / "deploy" / "provision.sh"
        result = subprocess.run([bash, "-n", str(script)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
```

Run: `python -m unittest tests.test_deploy -v`. Expected: `ImportError: cannot import name 'deploy'`.

- [ ] **Step 2: Write `tools/deploy.py`.**

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Deploys the update server: copies a bundle to the server, runs provision.sh.

    python tools/deploy.py --host root@203.0.113.5 --address 203.0.113.5 --cup-key FILE

The first run is as root on a fresh Debian 12 server. provision.sh creates
the administrator `ghost` and turns off root's SSH login, so later runs use
--host ghost@<address>. Each run changes only what differs.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REMOTE = "/tmp/ghost-update-deploy"
_ADDRESS = re.compile(r"^[A-Za-z0-9.-]+$")


def bundle_files(repo: Path = REPO) -> list[Path]:
    package = [p for p in (repo / "ghost_update").rglob("*.py")]
    deployment = [p for p in (repo / "deploy").iterdir() if p.is_file()]
    return sorted(p.relative_to(repo) for p in package + deployment)


def write_bundle(out: Path, cup_key: Path, repo: Path = REPO) -> Path:
    with tarfile.open(out, "w:gz") as archive:
        for rel in bundle_files(repo):
            archive.add(repo / rel, arcname=rel.as_posix())
        archive.add(cup_key, arcname="cup_key.json")
    return out


def commands(bundle: Path, host: str, address: str) -> list[list[str]]:
    if not _ADDRESS.match(address):
        raise ValueError("the address is an IPv4 address or a host name")
    remote = (f"rm -rf {REMOTE} && mkdir -m 700 {REMOTE} && tar -xzf {REMOTE}.tar.gz -C {REMOTE}"
              f" && rm {REMOTE}.tar.gz && sudo bash {REMOTE}/deploy/provision.sh --address "
              f"{address} --bundle {REMOTE}; status=$?; rm -rf {REMOTE}; exit $status")
    return [["scp", "-q", str(bundle), f"{host}:{REMOTE}.tar.gz"], ["ssh", host, remote]]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", required=True, help="USER@HOST to sign in as")
    parser.add_argument("--address", required=True, help="the server's public address")
    parser.add_argument("--cup-key", type=Path, required=True,
                        help="the CUP private key file (the test identity's, for now)")
    args = parser.parse_args(argv)
    with tempfile.TemporaryDirectory() as tmp:
        bundle = write_bundle(Path(tmp) / "ghost-update-deploy.tar.gz", args.cup_key)
        for step in commands(bundle, args.host, args.address):
            code = subprocess.run(step).returncode
            if code:
                print(f"deploy: {step[0]} exited with {code}", file=sys.stderr)
                return code
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Write `deploy/provision.sh`.**

```bash
#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Provisions Ghost's update server on Debian 12, or brings it back in line.
# tools/deploy.py runs it as root:
#   provision.sh --address <public address> --bundle <unpacked bundle>
# Each step changes only what differs and says so. A second run reports
# "provision: 0 change(s)".
set -euo pipefail

ADDRESS="" BUNDLE=""
while [ $# -gt 0 ]; do
  case $1 in
    --address) ADDRESS=$2; shift 2 ;;
    --bundle) BUNDLE=$2; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ $ADDRESS =~ ^[A-Za-z0-9.-]+$ ]] || { echo "--address is required" >&2; exit 2; }
[ -d "$BUNDLE/deploy" ] || { echo "--bundle is required" >&2; exit 2; }
[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 2; }

CHANGES=0
SERVICE_CHANGED=0
changed() { echo "changed: $*"; CHANGES=$((CHANGES + 1)); }

# install_file SRC DEST MODE [OWNER]: succeeds if it changed DEST.
install_file() {
  local src=$1 dest=$2 mode=$3 owner=${4:-root}
  if [ -f "$dest" ] && cmp -s "$src" "$dest" \
      && [ "$(stat -c '%a %U' "$dest")" = "$mode $owner" ]; then
    return 1
  fi
  install -D -m "$mode" -o "$owner" -g "$owner" "$src" "$dest"
  changed "$dest"
}

# rendered TEMPLATE: prints a temporary file with @ADDRESS@ replaced.
rendered() {
  local tmp
  tmp=$(mktemp)
  sed "s/@ADDRESS@/$ADDRESS/g" "$1" > "$tmp"
  echo "$tmp"
}

# ensure_dir PATH MODE OWNER
ensure_dir() {
  if [ -d "$1" ] && [ "$(stat -c '%a %U' "$1")" = "$2 $3" ]; then return; fi
  install -d -m "$2" -o "$3" -g "$3" "$1"
  changed "$1"
}

# 1. Packages. Debian 12 ships Caddy 2.6 and python3-cryptography 38.
missing=()
for p in caddy curl nftables python3 python3-cryptography sudo unattended-upgrades; do
  dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed" \
    || missing+=("$p")
done
if [ ${#missing[@]} -gt 0 ]; then
  apt-get update -q
  DEBIAN_FRONTEND=noninteractive apt-get install -y -q "${missing[@]}"
  changed "packages: ${missing[*]}"
fi
install_file "$BUNDLE/deploy/20auto-upgrades" /etc/apt/apt.conf.d/20auto-upgrades 644 || true

# 2. The administrator: SSH keys only, no root login.
if ! id ghost >/dev/null 2>&1; then
  useradd --create-home --shell /bin/bash ghost
  changed "user ghost"
fi
if [ ! -s /home/ghost/.ssh/authorized_keys ]; then
  install -d -m 700 -o ghost -g ghost /home/ghost/.ssh
  install -m 600 -o ghost -g ghost /root/.ssh/authorized_keys /home/ghost/.ssh/authorized_keys
  changed "ghost's SSH keys, copied from root's"
fi
visudo -cf "$BUNDLE/deploy/sudoers" >/dev/null
install_file "$BUNDLE/deploy/sudoers" /etc/sudoers.d/ghost 440 || true
if install_file "$BUNDLE/deploy/sshd.conf" /etc/ssh/sshd_config.d/ghost.conf 644; then
  sshd -t
  systemctl reload ssh
fi

# 3. The service, its configuration and its key.
while IFS= read -r f; do
  if install_file "$BUNDLE/$f" "/opt/ghost-update/$f" 644; then SERVICE_CHANGED=1; fi
done < <(cd "$BUNDLE" && find ghost_update -name '*.py' | sort)
install_file "$BUNDLE/deploy/ghost-update-admin" /usr/local/sbin/ghost-update-admin 755 || true
config=$(rendered "$BUNDLE/deploy/server.json")
if install_file "$config" /etc/ghost-update/server.json 644; then SERVICE_CHANGED=1; fi
rm -f "$config"
if install_file "$BUNDLE/cup_key.json" /etc/ghost-update/cup_key.json 600; then
  SERVICE_CHANGED=1
fi
if install_file "$BUNDLE/deploy/ghost-update.service" /etc/systemd/system/ghost-update.service 644; then
  systemctl daemon-reload
  SERVICE_CHANGED=1
fi
ensure_dir /srv/releases 755 root
ensure_dir /srv/releases/staging 755 ghost
if [ ! -f /srv/releases/releases.json ]; then
  /usr/local/sbin/ghost-update-admin init
  changed /srv/releases/releases.json
fi

# 4. Caddy and the firewall, each checked before it is installed.
caddyfile=$(rendered "$BUNDLE/deploy/Caddyfile")
caddy validate --config "$caddyfile" --adapter caddyfile >/dev/null 2>&1 \
  || { caddy validate --config "$caddyfile" --adapter caddyfile; exit 1; }
if install_file "$caddyfile" /etc/caddy/Caddyfile 644; then systemctl restart caddy; fi
rm -f "$caddyfile"
nft -c -f "$BUNDLE/deploy/nftables.conf"
if install_file "$BUNDLE/deploy/nftables.conf" /etc/nftables.conf 755; then
  systemctl restart nftables
fi

# 5. Enabled and running.
for unit in nftables caddy ghost-update; do
  if ! systemctl is-enabled --quiet "$unit"; then
    systemctl enable --quiet "$unit"
    changed "$unit enabled"
  fi
done
if [ "$SERVICE_CHANGED" = 1 ] || ! systemctl is-active --quiet ghost-update; then
  systemctl restart ghost-update
  changed "ghost-update restarted"
fi
for unit in nftables caddy; do
  if ! systemctl is-active --quiet "$unit"; then
    systemctl start "$unit"
    changed "$unit started"
  fi
done

# 6. Check: through Caddy, the service refuses a request without cup2key.
root_cert=/var/lib/caddy/.local/share/caddy/pki/authorities/local/root.crt
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  [ -f "$root_cert" ] && break
  sleep 1
done
code=$(curl -s -o /dev/null -w '%{http_code}' --cacert "$root_cert" -X POST --data '{}' \
  "https://$ADDRESS/update" || true)
if [ "$code" != 400 ]; then
  echo "check failed: POST /update without cup2key answered '$code', expected 400" >&2
  exit 1
fi
echo "provision: $CHANGES change(s)"
```

- [ ] **Step 4: Run all tests and lint; they pass. Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m unittest discover -s tests -t . 2>&1 | tail -1
git add -A && python tools/lint.py && git commit -q -s -m "deploy: provision.sh and tools/deploy.py

provision.sh installs Debian's packages, the administrator, the service,
its key, Caddy and the firewall, each only when it differs; Caddy's and
nftables' files are checked before they're installed, and the run ends
by asking the service a question through Caddy. deploy.py copies a
bundle with the CUP key and removes it afterwards."
```

Expected: `OK`, `lint: 0 problem(s)`.

---

### Task 11: Mutation checks of the repository's tests

Each change is made, the named tests run, and the change undone with `git checkout -- <file>`. Each must fail as stated.

| Change | Run | Must fail |
|---|---|---|
| In `ghost_update/service.py`, drop the `"X-Cup-Server-Proof": …` entry from the 200 response's headers | `tests.test_service` | `test_an_update_is_offered_and_signed` (`proof` is `None`) |
| In `deploy/Caddyfile`, delete `header_up -X-Forwarded-For` | `tests.test_deploy_files` | `test_the_proxy_passes_no_client_address` |
| In `ghost_update/protocol.py`, change `installed >= parse_version(release.version)` to `installed > parse_version(release.version)` | `tests.test_protocol` | `test_the_same_or_a_newer_version_gets_noupdate` |
| In `ghost_update/service.py`, make `log_message` call `super().log_message(format, *args)` | `tests.test_service` | `test_nothing_from_a_request_is_written` (`127.0.0.1` written) |

- [ ] **Step 1: Run each; record the failing test names.**
- [ ] **Step 2: `git status --short` shows nothing.** The results go in the spike notes (Task 17).

---

### Task 12: Publish the repository

Needs the user's answer to Task 0's first question.

- [ ] **Step 1: The remote.** If the user allowed it:

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && gh repo create robyroro/project-ghost-update-server --public --description "Project Ghost's update server: Omaha 4, CUP-signed, no record of who asked" --source . --remote origin
```

Otherwise `git remote add origin https://github.com/robyroro/project-ghost-update-server.git` after the user creates it.

- [ ] **Step 2: Push, then check CI.**

```bash
git push -u origin main && gh run list --limit 1
```

Expected: the `tests` workflow finishes successfully on Ubuntu and Windows. If Ubuntu fails on `cryptography==38.0.4`, read the log; that version is the one the VPS runs, so the fix belongs in the code, not the pin.

---

### Task 13: Provision the VPS

Needs the VPS address from Task 0's second question. `VPS` below is that address.

- [ ] **Step 1: Reach it.**

```bash
ssh -o StrictHostKeyChecking=accept-new root@$VPS 'cat /etc/debian_version; uname -m'
```

Expected: `12.x` and `x86_64`. The host key is accepted on first use; tell the user its fingerprint (`ssh-keygen -lf <(ssh-keyscan -t ed25519 $VPS 2>/dev/null)`) so that they can compare it with the provider's console.

- [ ] **Step 2: First run, as root.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python tools/deploy.py --host root@$VPS --address $VPS --cup-key /c/Users/robyv/Desktop/DLU/webops/test/updater/cup_test_key.json
```

Expected: `changed:` lines for the packages, the user `ghost`, each file and directory, the enabled units, then `provision: N change(s)` with N > 0. Root's SSH login is now off.

- [ ] **Step 3: Second run, as the administrator; nothing may change.**

```bash
python tools/deploy.py --host ghost@$VPS --address $VPS --cup-key /c/Users/robyv/Desktop/DLU/webops/test/updater/cup_test_key.json
```

Expected: no `changed:` line and `provision: 0 change(s)`. Any change is a bug in `provision.sh`: fix it in a commit, then run Step 3 again until it passes.

- [ ] **Step 4: Root can't sign in; the service runs as designed.**

```bash
ssh -o BatchMode=yes root@$VPS true; echo "root exit: $?"
ssh ghost@$VPS 'systemctl is-active caddy nftables ghost-update; sudo ghost-update-admin list; sudo nft list ruleset | grep -c "timeout 60s"; systemctl show ghost-update -p User,DynamicUser'
```

Expected: `root exit: 255`; `active` three times; both app IDs with `active none`; `2`; `DynamicUser=yes`.

---

### Task 14: The update URL as a build input (`WEBOPS`)

**Files:**
- Modify: `branding/updater.gni` (the header comment, and `update_check_url`)

- [ ] **Step 1: Make `update_check_url` come from a GN argument.** In `branding/updater.gni`, replace the line `update_check_url = "http://127.0.0.1:8484/update"` with:

```
declare_args() {
  # The update server. The default is tools/update_server.py on loopback,
  # which the local end-to-end test runs; the remote test builds
  # out/updater_remote with the server's address (docs/build/windows.md).
  ghost_update_url = "http://127.0.0.1:8484/update"
}
update_check_url = ghost_update_url
```

In the header comment, replace "the names, GUIDs and the loopback update URL are" with "the names and GUIDs are".

- [ ] **Step 2: The existing output directories don't change.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && rm -rf chromium/src/ghost/branding && cp -r branding chromium/src/ghost/branding
cd chromium/src && export PATH="/c/src/depot_tools:$PATH" DEPOT_TOOLS_WIN_TOOLCHAIN=0
cmd //c "gn args out\\updater --list=ghost_update_url --short"
s=$(date +%s) && cmd //c "autoninja.bat -C out\\updater -j 10 chrome/updater/win:updater" 2>&1 | tr '\r' '\n' | grep -E "finished|error:" | tail -1; echo "$(( $(date +%s) - s ))s"
```

Expected: `ghost_update_url = "http://127.0.0.1:8484/update"`, and the build finishes within a minute: the value didn't change, so nothing but GN's files is regenerated. (A siso dry run is not evidence here; one has misreported before.)

- [ ] **Step 3: Commit in `WEBOPS`; bring `src/ghost` back to the commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add branding/updater.gni && python tools/lint.py && git commit -q -m "branding: the update URL is a build argument

ghost_update_url defaults to the test server on loopback, so out/updater
and the local end-to-end test are unchanged; the remote test builds its
own output directory with the update server's address."
cd chromium/src/ghost && git checkout -q -- . && git pull -q && git log --oneline -1
```

- [ ] **Step 4: Create and build `out/updater_remote` (about 45 minutes, in the background).**

```bash
cd /c/Users/robyv/Desktop/DLU/webops/chromium/src && export PATH="/c/src/depot_tools:$PATH" DEPOT_TOOLS_WIN_TOOLCHAIN=0
mkdir -p out/updater_remote && cat > out/updater_remote/args.gn <<EOF
import("//ghost/build/args/dev.gn")

# The updater and its metainstaller run alone on a machine, so they cannot
# depend on the component build's DLLs.
is_component_build = false

# The update server on the VPS (sub-project C's remote end-to-end test).
ghost_update_url = "https://$VPS/update"
EOF
cmd //c "gn gen out\\updater_remote" && s=$(date +%s) && cmd //c "autoninja.bat -C out\\updater_remote -j 10 chrome/updater/win/installer:installer chrome/updater/win:signing chrome/updater/win:updater" 2>&1 | tr '\r' '\n' | grep -E "finished|error:|FAILED" | tail -3; echo "$(( $(date +%s) - s ))s"
```

Expected: `The build has finished successfully.` Record the time for the spike notes. Tasks 15's code steps can run while it builds; Task 15's tests don't need it.

---

### Task 15: The end-to-end test's remote mode (`WEBOPS`)

**Files:**
- Modify: `tools/installer_smoke.py` (`sandbox_config`)
- Modify: `tools/update_smoke.py` (`foreign_urls`, `run`, `run_in_sandbox`, `main`; new constants and helpers)
- Modify: `tools/tests/test_update_smoke.py`

Don't run a Sandbox while editing `tools/`.

- [ ] **Step 1: Write the failing tests.** Append to `tools/tests/test_update_smoke.py`, before `if __name__ == "__main__":`:

```python
class RemoteModeTest(unittest.TestCase):
    def test_the_server_is_an_allowed_host(self):
        log = ("a https://203.0.113.5/update b https://203.0.113.5/releases/x.crx3\n"
               "c http://127.0.0.1:8484/update d https://example.com/a\n")
        self.assertEqual(update_smoke.foreign_urls(log, ("https://203.0.113.5",)),
                         ["http://127.0.0.1:8484/update", "https://example.com/a"])

    def test_client_address(self):
        self.assertEqual(update_smoke.client_address("198.51.100.7 51234 203.0.113.5 22\n"),
                         "198.51.100.7")
        with self.assertRaises(ValueError):
            update_smoke.client_address("")

    def test_the_sandbox_gets_network_only_when_asked(self):
        import installer_smoke
        from pathlib import Path
        args = (Path("i"), Path("t"), Path("p"), Path("r"))
        self.assertIn("<Networking>Disable</Networking>", installer_smoke.sandbox_config(*args))
        self.assertIn("<Networking>Enable</Networking>",
                      installer_smoke.sandbox_config(*args, networking=True))

    def test_argument_combinations(self):
        def problem(*argv):
            return update_smoke.argument_problem(update_smoke.parser().parse_args(
                ["sandbox", "--release-version", "1.0.0.1", "--appid", "{a}", *argv]))
        self.assertIsNone(problem("--offline-installer", "o.exe", "--update-crx", "u.crx3",
                                  "--update-version", "1.0.0.2"))
        self.assertIsNone(problem("--offline-installer", "o.exe", "--update-version", "1.0.0.2",
                                  "--server", "https://203.0.113.5"))
        self.assertIsNone(problem("--online-installer", "u.exe", "--server",
                                  "https://203.0.113.5"))
        for argv in ((), ("--offline-installer", "o.exe", "--online-installer", "u.exe"),
                     ("--offline-installer", "o.exe", "--update-version", "1.0.0.2"),
                     ("--online-installer", "u.exe"),
                     ("--online-installer", "u.exe", "--server", "https://h",
                      "--update-version", "1.0.0.2")):
            with self.subTest(argv):
                self.assertIsNotNone(problem(*argv))
```

Run: `cd $WEBOPS && python -m unittest discover -s tools/tests -t tools -p "test_update_smoke.py" 2>&1 | tail -3`. Expected: errors (`foreign_urls() takes 1 positional argument`, no `client_address`, no `parser`).

- [ ] **Step 2: `tools/installer_smoke.py`.** Change `sandbox_config`'s signature and its networking line:

```python
def sandbox_config(installer_dir: Path, tools_dir: Path, python_dir: Path, results_dir: Path,
                   installer_name: str = "mini_installer.exe",
                   script_args: str | None = None, networking: bool = False) -> str:
```

```python
            f"  <Networking>{'Enable' if networking else 'Disable'}</Networking>\n"
```

Add to its docstring: "The sandbox has no network unless `networking` is set."

- [ ] **Step 3: `tools/update_smoke.py`.** Update the module docstring's usage to:

```
  sandbox (--offline-installer O | --online-installer U) --release-version V1
          [--update-crx C] [--update-version V2] --appid A
          [--server URL [--server-ssh USER@HOST]]
      on the build machine: run the test in a fresh Windows Sandbox. Without
      --server it runs tools/update_server.py in the sandbox; with it, the
      sandbox gets network access and uses that server, whose root
      certificate --server-ssh fetches, and which it then checks for the
      test machine's address.
  run     the test itself, in the sandbox
```

Add after `_URL_RE`:

```python
OFFLINE_INSTALLER = "ProjectGhostOfflineSetup.exe"
ONLINE_INSTALLER = "UpdaterSetup.exe"
SERVER_ROOT_CERT = "server_root.crt"
LOCAL_SERVER = "http://127.0.0.1"
# Caddy's internal CA on the update server (sub-project C), until it has a domain.
CADDY_ROOT_CERT = "/var/lib/caddy/.local/share/caddy/pki/authorities/local/root.crt"
```

Replace `foreign_urls`:

```python
def foreign_urls(log: str, allowed: tuple[str, ...] = (LOCAL_SERVER,)) -> list[str]:
    return [url for url in _URL_RE.findall(log)
            if not url.startswith(allowed + _XML_NAMESPACE_PREFIXES)]


def client_address(ssh_connection: str) -> str:
    """This machine's address as a server sees it: SSH_CONNECTION's first field."""
    fields = ssh_connection.split()
    if len(fields) != 4:
        raise ValueError("unexpected SSH_CONNECTION")
    return fields[0]


def _ssh(host: str, command: str) -> subprocess.CompletedProcess:
    return subprocess.run(["ssh", host, command], capture_output=True, timeout=120)


def server_records(host: str) -> list[str]:
    """Where the server holds this machine's address, outside admin records."""
    address = client_address(_ssh(host, "echo $SSH_CONNECTION").stdout.decode())
    out = _ssh(host, f"sudo ghost-update-admin find-address {address}")
    if out.returncode == 0:
        return []
    return out.stdout.decode().splitlines() or [f"find-address exited with {out.returncode}"]
```

Replace `run` with:

```python
def run(payload: Path, results: Path, exp: smoke.Expectations, appid: str,
        update_version: str | None, server: str | None = None,
        installer: str = OFFLINE_INSTALLER) -> dict:
    """Without `server`, serves update_version from payload/update.crx3 on loopback.
    Without `update_version` (the online installer), installs and checks only."""
    result = {"expectations": exp.__dict__, "steps": []}
    log = results / "requests.jsonl"
    final = replace(exp, release_version=update_version) if update_version else exp

    def step(name: str, failures: list[str], **details) -> bool:
        result["steps"].append({"name": name, "failures": failures, **details})
        return not failures

    local = None
    if server is None:
        local = update_server.UpdateServer(
            ("127.0.0.1", 8484),
            update_server.Offer(appid, update_version, payload / "update.crx3",
                                "mini_installer.exe", "--verbose-logging --do-not-launch-chrome"),
            update_server.load_key(payload / "cup_test_key.json"), log)
        threading.Thread(target=local.serve_forever, daemon=True).start()
    try:
        if not step("clean machine", smoke.evaluate_uninstalled(smoke.snapshot(exp), exp)
                    + (["an updater is already installed"] if _updater_exe(exp) else [])):
            return result

        root_cert = payload / SERVER_ROOT_CERT
        if root_cert.exists():
            out = subprocess.run(["certutil", "-addstore", "-f", "Root", str(root_cert)],
                                 capture_output=True, text=True)
            if not step("trust the server", [] if out.returncode == 0 else [
                    f"certutil exited with {out.returncode}: {out.stdout.strip()[-300:]}"]):
                return result

        work = Path(tempfile.mkdtemp(prefix="installer-"))
        setup = Path(shutil.copy(payload / installer, work))
        code = subprocess.run([str(setup), *offline_installer.install_arguments(appid),
                               "--enable-logging"], timeout=1800).returncode
        installed = smoke.snapshot(exp)
        failures = [] if code == 0 else [f"{installer} exited with {code}"]
        failures += smoke.evaluate_installed(installed, exp)
        updater = _updater_exe(exp)
        failures += [] if updater else ["no updater.exe under the company directory"]
        failures += [] if _updater_tasks(exp) else ["no scheduled task for the updater"]
        pv = _registered_version(exp, appid)
        failures += [] if pv == exp.release_version else [
            f"Clients\\{appid} pv is {pv!r}, expected {exp.release_version!r}"]
        if not step("install", failures, snapshot=installed):
            return result
        app_dir = Path(installed["chrome_exe"]).parent

        if update_version:
            code = subprocess.run([str(updater), "--update-apps", "--enable-logging"],
                                  timeout=1800).returncode
            reached = _wait(lambda: _registered_version(exp, appid) == update_version, 1200)
            failures = [] if reached else [
                f"pv stayed {_registered_version(exp, appid)!r}, expected {update_version!r}"
                f" (updater exited with {code})"]
            failures += [] if (app_dir / update_version).is_dir() else [
                f"no {update_version} directory beside chrome.exe"]
            if not step("update", failures):
                return result
        step("launch", smoke.launch(Path(installed["chrome_exe"]), final))

        updater_log = "".join(p.read_text(encoding="utf-8", errors="replace")
                              for p in _company_dir(exp).rglob("updater*.log"))
        failures = [f"the updater's log names {url}" for url in foreign_urls(
            updater_log, (LOCAL_SERVER,) if server is None else (server,))]
        if server is None:
            lines = log.read_text(encoding="utf-8").splitlines() if log.exists() else []
            failures = evaluate_requests(lines) + failures
        step("privacy", failures)

        setup_exe = app_dir / final.release_version / "Installer" / "setup.exe"
        code = subprocess.run([str(setup_exe), "--uninstall", "--force-uninstall",
                               "--verbose-logging"], timeout=900).returncode
        failures = ([] if code == smoke.UNINSTALL_SUCCESSFUL else
                    [f"setup.exe --uninstall exited with {code}"])
        # --wake notices the app is gone, then starts --uninstall-if-unused
        # itself; run alone, --uninstall-if-unused still counts the app.
        subprocess.run([str(updater), "--wake", "--enable-logging"], timeout=900)
        gone = _wait(lambda: _updater_exe(exp) is None, 300)
        uninstalled = smoke.snapshot(exp)
        failures += smoke.evaluate_uninstalled(uninstalled, exp)
        failures += [] if gone else ["the updater did not remove itself"]
        failures += ["the updater's scheduled task is left"] if _updater_tasks(exp) else []
        failures += ([f"Software\\{exp.company_path}\\Update is left"]
                     if _updater_key_exists(exp) else [])
        step("uninstall", failures, snapshot=uninstalled, updater_key=_updater_key_tree(exp))
    except Exception as e:  # reported, so the build machine learns why
        step("error", [f"{type(e).__name__}: {e}"])
    finally:
        if local:
            local.shutdown()
        for name in ("chrome_installer.log",):
            path = Path(tempfile.gettempdir()) / name
            if path.exists():
                shutil.copy(path, results / name)
        for path in _company_dir(exp).rglob("updater*.log") if exp.company_path else []:
            shutil.copy(path, results / path.name)
        partial = results / (RESULT_FILE + ".partial")
        partial.write_text(json.dumps(result, indent=1), encoding="utf-8")
        partial.replace(results / RESULT_FILE)
    return result
```

The waits are longer than the local test's (1800 s for the installer, 1200 s for `pv`): the package comes over the internet.

Replace `run_in_sandbox` with:

```python
def run_in_sandbox(installer: Path, crx: Path | None, release_version: str,
                   update_version: str | None, appid: str, timeout: int,
                   server: str | None = None, server_ssh: str | None = None) -> int:
    payload = Path(tempfile.mkdtemp(prefix="update-payload-"))
    shutil.copy(installer, payload / installer.name)
    if crx:
        shutil.copy(crx, payload / "update.crx3")
    # Only tools/ is mapped into the sandbox; the server's key travels with the payload.
    shutil.copy(update_server.CUP_KEY_FILE, payload / "cup_test_key.json")
    if server_ssh:
        cert = _ssh(server_ssh, f"sudo cat {CADDY_ROOT_CERT}")
        if cert.returncode or not cert.stdout:
            print(f"could not fetch the server's root certificate over SSH", file=sys.stderr)
            return 1
        (payload / SERVER_ROOT_CERT).write_bytes(cert.stdout)
    results = Path(tempfile.mkdtemp(prefix="update-smoke-"))
    exp = smoke.expectations(repo.REPO_ROOT, release_version)
    (results / EXPECTATIONS_FILE).write_text(json.dumps(exp.__dict__), encoding="utf-8")
    script = (f"update_smoke.py run --payload {smoke._IN_SANDBOX['installer']} "
              f"--results {smoke._IN_SANDBOX['results']} --appid {appid} "
              f"--installer {installer.name}"
              + (f" --update-version {update_version}" if update_version else "")
              + (f" --server {server}" if server else ""))
    config = results.with_suffix(".wsb")
    config.write_text(smoke.sandbox_config(payload, TOOLS_DIR, Path(sys.base_prefix), results,
                                           script_args=script, networking=server is not None),
                      encoding="utf-8")
    print(f"starting Windows Sandbox; results in {results}", flush=True)
    subprocess.Popen([str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
                          / "WindowsSandbox.exe"), str(config)])
    deadline = time.monotonic() + timeout
    while not (results / RESULT_FILE).exists():
        if time.monotonic() > deadline:
            print(f"no result within {timeout}s", file=sys.stderr)
            return 1
        time.sleep(5)
    result = smoke.read_result(results)
    print(smoke.format_result(result))
    passed = smoke.passed(result)
    if server_ssh:
        found = server_records(server_ssh)
        print("ok      the server holds no record of this machine's address" if not found else
              "FAILED  the server holds this machine's address\n"
              + "\n".join(f"          - {place}" for place in found))
        passed = passed and not found
    return 0 if passed else 1
```

Replace `main` with:

```python
def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="command", required=True)
    s = sub.add_parser("sandbox")
    s.add_argument("--offline-installer", type=Path)
    s.add_argument("--online-installer", type=Path)
    s.add_argument("--release-version", required=True,
                   help="the version the installer installs")
    s.add_argument("--update-crx", type=Path)
    s.add_argument("--update-version")
    s.add_argument("--appid", required=True)
    s.add_argument("--server", help="an update server's base URL, such as https://203.0.113.5")
    s.add_argument("--server-ssh", help="USER@HOST of that server")
    s.add_argument("--timeout", type=int, default=3600)
    r = sub.add_parser("run")
    r.add_argument("--payload", type=Path, required=True)
    r.add_argument("--results", type=Path, required=True)
    r.add_argument("--appid", required=True)
    r.add_argument("--installer", default=OFFLINE_INSTALLER)
    r.add_argument("--update-version")
    r.add_argument("--server")
    return p


def argument_problem(args: argparse.Namespace) -> str | None:
    if (args.offline_installer is None) == (args.online_installer is None):
        return "pass one of --offline-installer and --online-installer"
    if args.online_installer:
        if not args.server:
            return "the online installer needs --server"
        if args.update_version:
            return "the online installer installs the server's release; no --update-version"
        return None
    if not args.update_version:
        return "the offline installer's test needs --update-version"
    if not args.server and not args.update_crx:
        return "without --server, the test serves --update-crx itself"
    return None


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    if args.command == "sandbox":
        problem = argument_problem(args)
        if problem:
            print(problem, file=sys.stderr)
            return 2
        installer = args.offline_installer or args.online_installer
        return run_in_sandbox(installer, None if args.server else args.update_crx,
                              args.release_version, args.update_version, args.appid,
                              args.timeout, args.server, args.server_ssh)
    if not smoke.is_disposable(os.environ.get("USERNAME", ""), False):
        print("run installs into this user's profile; use `sandbox`", file=sys.stderr)
        return 2
    exp = smoke.Expectations(**json.loads(
        (args.results / EXPECTATIONS_FILE).read_text(encoding="utf-8")))
    result = run(args.payload, args.results, exp, args.appid, args.update_version,
                 args.server, args.installer)
    print(smoke.format_result(result))
    return 0 if smoke.passed(result) else 1
```

The online installer is `UpdaterSetup.exe` itself, so the file copied into the payload keeps its name and `--installer` names it.

- [ ] **Step 4: Run the tooling tests and lint; they pass. Commit.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "tools: the updater's end-to-end test against a remote server

With --server the sandbox gets network access, trusts the server's root
certificate fetched over SSH, and takes the update from that server; the
test then checks the server for this machine's address. The online
installer installs the server's release. Without --server nothing
changes."
```

Expected: `lint: 0 problem(s)`, `OK`.

---

### Task 16: The remote end-to-end runs

`S` is a scratch directory outside both repositories, for example the session's scratchpad. In each shell:

```bash
S=<scratch directory>; VPS=<the VPS's address>; APPID="{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}"
```

- [ ] **Step 1: The inputs.** The offline installer of respin `-1`, from `out/updater_remote` (Task 14 Step 4 must have finished), and the CRX3 of respin `-2`.

```bash
cd /c/Users/robyv/Desktop/DLU/webops && export PATH="/c/src/depot_tools:$PATH" DEPOT_TOOLS_WIN_TOOLCHAIN=0
trap 'git -C chromium/src checkout -- chrome/VERSION' EXIT
python tools/release_version.py write --src chromium/src --tag 152.0.7977.149-2
(cd chromium/src && cmd //c "autoninja.bat -C out\\vanilla -j 10 mini_installer" 2>&1 | tr '\r' '\n' | grep -E "finished|error:" | tail -1)
(cd tools && python update_server.py crx --installer ../chromium/src/out/vanilla/mini_installer.exe --out "$S/update.crx3")
python tools/release_version.py write --src chromium/src --tag 152.0.7977.149-1
(cd chromium/src && cmd //c "autoninja.bat -C out\\vanilla -j 10 mini_installer" 2>&1 | tr '\r' '\n' | grep -E "finished|error:" | tail -1)
python tools/offline_installer.py --src chromium/src --out out/updater_remote --installer chromium/src/out/vanilla/mini_installer.exe --version 152.0.7977.14901 --appid "$APPID" --output "$S/ProjectGhostOfflineSetup.exe"
git -C chromium/src checkout -- chrome/VERSION && ls -l "$S"
```

Expected: `update.crx3` and `ProjectGhostOfflineSetup.exe` in `$S`; `chrome/VERSION` restored. Each respin is about 7 minutes.

- [ ] **Step 2: Publish respin `-2`.**

```bash
cd /c/Users/robyv/Desktop/DLU/project-ghost-update-server && python -m ghost_update.release --crx "$S/update.crx3" --appid "$APPID" --version 152.0.7977.14902 --host ghost@$VPS && ssh ghost@$VPS sudo ghost-update-admin list
```

Expected: `{c0ff4371-…} 152.0.7977.14902 active`, then the list shows it.

- [ ] **Step 3: The offline installer against the VPS.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && python tools/update_smoke.py sandbox --offline-installer "$S/ProjectGhostOfflineSetup.exe" --release-version 152.0.7977.14901 --update-version 152.0.7977.14902 --appid "$APPID" --server https://$VPS --server-ssh ghost@$VPS
```

Expected: `ok` for clean machine, trust the server, install, update, launch, privacy and uninstall, then `PASSED` and `ok      the server holds no record of this machine's address`. Run it in the background; it takes a few minutes more than the local test, since the package is downloaded.

**If it fails, read the evidence before changing anything:** `run.log`, `result.json`, `updater*.log`, `chrome_installer.log` in the results directory, and `ssh ghost@$VPS journalctl -u caddy -u ghost-update --since -1h`. Fix the cause where it lies:
- **the updater refuses Caddy's internal certificate:** record what the log says; the alternative is the domain with ACME, which is the user's decision;
- **a request is refused (400 or 413):** the service's journal says why, without client data;
- **the server holds the address:** `find-address` names the place; a Caddy logger means the Caddyfile's `exclude` list is short, and the fix is there.

Record each failure and its fix for the spike notes, then re-run.

- [ ] **Step 4: The online installer.** Wait at least 3 minutes after Step 3's Sandbox closed.

```bash
python tools/update_smoke.py sandbox --online-installer chromium/src/out/updater_remote/UpdaterSetup.exe --release-version 152.0.7977.14902 --appid "$APPID" --server https://$VPS --server-ssh ghost@$VPS
```

Expected: `ok` for clean machine, trust the server, install, launch, privacy and uninstall; `PASSED`; the server holds no record of the address.

- [ ] **Step 5: The local test still passes.** Wait 3 minutes, then run B's local test with the inputs from B's spike or rebuilt as in [build/windows.md](../../build/windows.md#offline-installer). Expected: `PASSED`.

---

### Task 17: Documentation and spike notes (`WEBOPS`)

**Files:**
- Create: `docs/superpowers/specs/2026-10-03-update-server-spike.md`
- Modify: `docs/privacy-model.md`, `docs/architecture.md`, `docs/build/windows.md`, `docs/testing.md`, `docs/roadmap.md`, `docs/superpowers/specs/2026-10-03-update-server-design.md` (status)

- [ ] **Step 1: Spike notes,** in the format of B's ([2026-10-02-branded-updater-spike.md](../specs/2026-10-02-branded-updater-spike.md)): the date, the VPS (provider, size, Debian release; not its address, which changes with the domain), the result of each run in Task 16, the build time of `out/updater_remote`, the provisioning runs (changes in the first, 0 in the second), every failure and its fix, and the mutation results of Task 11.

- [ ] **Step 2: `docs/privacy-model.md`, "Data the browser sends".** Replace "The update server doesn't retain IP addresses." with:

```markdown
The update server doesn't retain IP addresses. Its code and configuration are public in [project-ghost-update-server](https://github.com/robyroro/project-ghost-update-server):
- the web server in front keeps no access log, and its own log leaves out request details, the address among them;
- the update service behind it receives requests from the web server only, so it never sees an address, and it writes nothing that comes from a request;
- the per-address rate limit is kept in kernel memory and forgets an address after a minute.

The end-to-end test checks this: after a test machine has installed and updated, it searches the server's logs and data for that machine's address.
```

- [ ] **Step 3: `docs/architecture.md`, "Updates and signed data".** After "The updater as built (Phase 2)", add a subsection "The update server (Phase 2)": the VPS, Caddy in front, the service on loopback, `releases.json` as the whole state, publishing with `ghost_update.release` and `ghost-update-admin activate`, the server holding the CUP key but not the publisher key, and the repository link.

- [ ] **Step 4: `docs/build/windows.md`.** In "Offline installer", add: the `ghost_update_url` argument; `out/updater_remote` and its first build time; the remote and online test commands of Task 16 Steps 3 and 4, with `<server address>` in place of the address.

- [ ] **Step 5: `docs/testing.md`.** Extend the "Updater end to end" row and its notes: the remote mode, the online installer, and the server's address check.

- [ ] **Step 6: Roadmap and spec.** Mark sub-project C done with the end-to-end result, linking the spec, the spike notes and the repository; tick "An update shipped end to end to test machines". Set the spec's status to implemented, with the spike notes' link.

- [ ] **Step 7: Commit; ask before pushing either repository.**

```bash
cd /c/Users/robyv/Desktop/DLU/webops && git add -A && python tools/lint.py && python -m unittest discover -s tools/tests -t tools 2>&1 | tail -1
git commit -q -m "docs: Ghost's update server, as built

The privacy model says how the server keeps no addresses and links its
repository. Architecture describes the server; the build guide, the
remote test; the spike notes record the runs, the provisioning and the
mutation checks; the roadmap marks sub-project C done and Phase 2's
first exit criterion met."
git log --oneline origin/main..main
```
