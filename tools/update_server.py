#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Omaha 4 test server for Ghost's updater under the test identity.

  keygen [--force]   new development keys (CUP, CRX publisher primary and
                     backup), then dev-files
  dev-files          rewrite branding/keys/dev.h, the CRX fixtures and
                     test/updater/cup_vector.json from the development keys
  vector             rewrite test/updater/cup_vector.json from the CUP key
  crx --installer I --out O [--key K]
                     pack an installer into a CRX3 signed with K (the
                     development publisher key by default)
  serve --crx C --version V --appid A [--port 8484] [--log L] [--cup-key K]
                     answer update checks, sign them with CUP, serve C

The server answers with an update when the request's version of the app is
older than V. It records every request body, so the end-to-end test can
check them against the allow-list. Loopback only; test keys only.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import http.server
import json
import sys
import urllib.parse
from dataclasses import dataclass
from pathlib import Path

import crx3
import ecdsa_p256
import repo
import signing

RESPONSE_PREFIX = ")]}'\n"
CUP_KEY_VERSION = 1
TEST_DIR = repo.REPO_ROOT / "test" / "updater"
CUP_KEY_FILE = TEST_DIR / "cup_test_key.json"
CRX_KEY_FILE = TEST_DIR / "crx_test_key.json"
CRX_BACKUP_KEY_FILE = TEST_DIR / "crx_test_backup_key.json"
CUP_VECTOR_FILE = TEST_DIR / "cup_vector.json"
FIXTURES_DIR = TEST_DIR / "data"
DEV_HEADER = signing.KEYS_DIR / "dev.h"
# The CRX verifier test's fixtures carry this file (updater/crx_verifier_unittest.cc).
FIXTURE_FILES = {"payload.txt": b"Ghost CRX verifier fixture\n"}
# RFC 6979's P-256 test key: public on purpose, trusted by no identity.
OTHER_KEY = 0xC9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721
_DAY_ZERO = datetime.date(2007, 1, 1)

# The keys Ghost's update requests may carry (spec: the request scrubber).
# None is a value; a dict is an object; a one-element list is a list of
# objects. components/update_client/request_scrubber.cc keeps the same keys;
# both are tested against test/updater/request_*.json.
_APP = {
    "appid": None, "version": None, "ap": None, "brand": None, "release_channel": None,
    "enabled": None, "disabled": [{"reason": None}], "cached_items": [{"sha256": None}],
    "updatecheck": {"updatedisabled": None, "rollback_allowed": None,
                    "sameversionupdate": None, "targetversionprefix": None},
    "data": [{"name": None, "index": None}],
}
ALLOWED = {"request": {
    "protocol": None, "ismachine": None, "acceptformat": None, "sessionid": None,
    "requestid": None, "@updater": None, "updaterversion": None, "prodversion": None,
    "updaterchannel": None, "prodchannel": None, "@os": None, "arch": None, "wow64": None,
    "dlpref": None, "os": {"platform": None, "arch": None, "version": None}, "apps": [_APP],
}}


def disallowed_keys(value, schema=ALLOWED, path: str = "") -> list[str]:
    if schema is None:
        return [f"{path} (not a value)"] if isinstance(value, (dict, list)) else []
    if isinstance(schema, list):
        if not isinstance(value, list):
            return [f"{path} (not a list)"]
        return [p for i, item in enumerate(value)
                for p in disallowed_keys(item, schema[0], f"{path}[{i}]")]
    if not isinstance(value, dict):
        return [f"{path} (not an object)"]
    out = []
    for key, item in value.items():
        child = f"{path}.{key}" if path else key
        out += [child] if key not in schema else disallowed_keys(item, schema[key], child)
    return out


def scrub(value, schema=ALLOWED):
    """The reference scrubber: what request_scrubber.cc must produce."""
    if schema is None:
        return value
    if isinstance(schema, list):
        return [scrub(item, schema[0]) for item in value]
    return {key: scrub(item, schema[key]) for key, item in value.items() if key in schema}


# --- Keys -------------------------------------------------------------------------------

load_key = signing.load_key_file

_DEV_COMMENT = ("Development identity only: {purpose}. Committed, so anyone can sign with "
                "it; only development builds pin it.")


def write_dev_files() -> None:
    cup = signing.file_signer(CUP_KEY_FILE)
    primary = signing.file_signer(CRX_KEY_FILE)
    backup = signing.file_signer(CRX_BACKUP_KEY_FILE)
    DEV_HEADER.parent.mkdir(parents=True, exist_ok=True)
    DEV_HEADER.write_text(signing.render_identity_header(
        "dev", CUP_KEY_VERSION, cup.public_der, [primary.public_der, backup.public_der],
        "tools/update_server.py dev-files"), encoding="utf-8", newline="\n")
    write_crx_fixtures(FIXTURES_DIR, primary, primary, backup)
    (FIXTURES_DIR / "other_publisher.crx3").write_bytes(
        crx3.build(FIXTURE_FILES, signing.scalar_signer("other", OTHER_KEY)))
    write_vector(load_key(CUP_KEY_FILE), CUP_KEY_VERSION, CUP_VECTOR_FILE)


def write_crx_fixtures(out_dir: Path, developer: signing.Signer, primary: signing.Signer,
                       backup: signing.Signer) -> None:
    """An identity's two fixtures: a package by its primary, one by its backup."""
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "ghost_publisher.crx3").write_bytes(
        crx3.build(FIXTURE_FILES, developer, [primary]))
    (out_dir / "ghost_backup_publisher.crx3").write_bytes(
        crx3.build(FIXTURE_FILES, developer, [backup]))


# --- CUP -------------------------------------------------------------------------------

def cup_proof(key: int, cup2key: str, request_body: bytes, response_body: bytes) -> str:
    """X-Cup-Server-Proof for a response, as client_update_protocol/cup.cc checks it."""
    request_hash = hashlib.sha256(request_body).digest()
    inner = hashlib.sha256(request_hash + hashlib.sha256(response_body).digest()
                           + cup2key.encode()).digest()
    signature = ecdsa_p256.der_signature(*ecdsa_p256.sign(key, inner))
    return f"{signature.hex()}:{request_hash.hex()}"


def cup_verify(public: tuple[int, int], cup2key: str, request_body: bytes,
               response_body: bytes, proof: str) -> bool:
    signature_hex, _, hash_hex = proof.partition(":")
    request_hash = hashlib.sha256(request_body).digest()
    if bytes.fromhex(hash_hex) != request_hash:
        return False
    inner = hashlib.sha256(request_hash + hashlib.sha256(response_body).digest()
                           + cup2key.encode()).digest()
    return ecdsa_p256.verify(public, inner,
                             *ecdsa_p256.parse_der_signature(bytes.fromhex(signature_hex)))


def write_vector(key: int, version: int, path: Path,
                 generator: str = "tools/update_server.py vector") -> None:
    request = '{"request":{"protocol":"4.0"}}'
    response = RESPONSE_PREFIX + '{"response":{"protocol":"4.0"}}'
    cup2key = f"{version}:12345"
    path.write_text(json.dumps({
        "comment": f"Generated by {generator}. Checked by "
                   "ghost/updater/cup_unittest.cc with Chromium's CUP verifier.",
        "nonce": 12345, "cup2key": cup2key, "request": request, "response": response,
        "proof": cup_proof(key, cup2key, request.encode(), response.encode())}, indent=2) + "\n",
        encoding="utf-8", newline="\n")


# --- Responses ------------------------------------------------------------------------

@dataclass(frozen=True)
class Offer:
    appid: str
    version: str
    crx: Path
    installer: str
    arguments: str


def _version_tuple(text: str) -> tuple[int, ...]:
    try:
        return tuple(int(p) for p in text.split("."))
    except ValueError:
        return ()


def respond(request: dict, offer: Offer | None, base_url: str, today: datetime.date) -> dict:
    apps = []
    for app in request.get("request", {}).get("apps", []):
        appid = app.get("appid", "")
        entry = {"appid": appid, "status": "ok"}
        if "updatecheck" in app:
            if (offer and appid.lower() == offer.appid.lower()
                    and _version_tuple(app.get("version", "")) < _version_tuple(offer.version)):
                data = offer.crx.read_bytes()
                digest = hashlib.sha256(data).hexdigest()
                entry["updatecheck"] = {
                    "status": "ok", "nextversion": offer.version,
                    "pipelines": [{"pipeline_id": "full", "operations": [
                        {"type": "download", "size": len(data), "out": {"sha256": digest},
                         "urls": [{"url": f"{base_url}/download/{offer.crx.name}"}]},
                        {"type": "crx3", "in": {"sha256": digest}, "path": offer.installer,
                         "arguments": offer.arguments}]}]}
            else:
                entry["updatecheck"] = {"status": "noupdate"}
        apps.append(entry)
    return {"response": {"protocol": "4.0", "server": "ghost-test",
                         "daystart": {"elapsed_days": (today - _DAY_ZERO).days},
                         "apps": apps}}


# --- HTTP -----------------------------------------------------------------------------

class _Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"  # BITS downloads need HTTP/1.1 and ranges
    server: UpdateServer

    def do_POST(self) -> None:
        url = urllib.parse.urlsplit(self.path)
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        query = urllib.parse.parse_qs(url.query)
        self.server.record(self.path, body)
        if url.path != "/update":
            self._send(404, b"")
            return
        try:
            request = json.loads(body)
        except ValueError:
            self._send(400, b"")
            return
        payload = (RESPONSE_PREFIX + json.dumps(respond(
            request, self.server.offer, self.server.base_url, datetime.date.today()))).encode()
        headers = {"Content-Type": "application/json"}
        if "cup2key" in query:
            headers["X-Cup-Server-Proof"] = cup_proof(self.server.cup_key, query["cup2key"][0],
                                                      body, payload)
        self._send(200, payload, headers)

    def _download(self, head: bool) -> None:
        offer = self.server.offer
        if not offer or self.path != f"/download/{offer.crx.name}":
            self._send(404, b"", head=head)
            return
        data = offer.crx.read_bytes()
        headers = {"Content-Type": "application/octet-stream", "Accept-Ranges": "bytes"}
        spec = self.headers.get("Range", "")
        if spec.startswith("bytes="):
            first, _, last = spec[6:].partition("-")
            start = int(first or 0)
            end = min(int(last), len(data) - 1) if last else len(data) - 1
            headers["Content-Range"] = f"bytes {start}-{end}/{len(data)}"
            self._send(206, data[start:end + 1], headers, head)
        else:
            self._send(200, data, headers, head)

    def do_GET(self) -> None:
        self._download(head=False)

    def do_HEAD(self) -> None:
        self._download(head=True)

    def _send(self, status: int, body: bytes, headers: dict | None = None,
              head: bool = False) -> None:
        self.send_response(status)
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if not head:
            self.wfile.write(body)

    def log_message(self, format: str, *args) -> None:
        pass  # request bodies go to the JSONL log instead


class UpdateServer(http.server.ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], offer: Offer | None, cup_key: int,
                 log: Path | None):
        super().__init__(address, _Handler)
        self.offer, self.cup_key, self.log = offer, cup_key, log
        self.base_url = f"http://{address[0]}:{self.server_address[1]}"

    def record(self, path: str, body: bytes) -> None:
        if not self.log:
            return
        try:
            parsed = json.loads(body) if body else None
        except ValueError:
            parsed = {"unparsed": body.decode("utf-8", "replace")}
        with self.log.open("a", encoding="utf-8") as f:
            f.write(json.dumps({"path": path, "body": parsed}) + "\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    keygen = sub.add_parser("keygen")
    keygen.add_argument("--force", action="store_true")
    sub.add_parser("dev-files")
    sub.add_parser("vector")
    crx = sub.add_parser("crx")
    crx.add_argument("--installer", type=Path, required=True)
    crx.add_argument("--out", type=Path, required=True)
    crx.add_argument("--key", type=Path, default=CRX_KEY_FILE)
    serve = sub.add_parser("serve")
    serve.add_argument("--crx", type=Path, required=True)
    serve.add_argument("--version", required=True)
    serve.add_argument("--appid", required=True)
    serve.add_argument("--installer", default="mini_installer.exe")
    serve.add_argument("--arguments", default="--verbose-logging --do-not-launch-chrome")
    serve.add_argument("--port", type=int, default=8484)
    serve.add_argument("--log", type=Path)
    serve.add_argument("--cup-key", type=Path, default=CUP_KEY_FILE)
    args = parser.parse_args(argv)

    if args.command == "keygen":
        keys = (CUP_KEY_FILE, CRX_KEY_FILE, CRX_BACKUP_KEY_FILE)
        if any(k.exists() for k in keys) and not args.force:
            print("development keys exist; pass --force to replace them", file=sys.stderr)
            return 1
        for path, purpose in zip(keys, ("signs CUP responses",
                                        "the CRX publisher key, also the developer key",
                                        "the backup CRX publisher key")):
            signing.write_key_file(path, ecdsa_p256.generate_private_key(),
                                   _DEV_COMMENT.format(purpose=purpose))
        write_dev_files()
    elif args.command == "dev-files":
        write_dev_files()
    elif args.command == "vector":
        write_vector(load_key(CUP_KEY_FILE), CUP_KEY_VERSION, CUP_VECTOR_FILE)
    elif args.command == "crx":
        args.out.write_bytes(crx3.build({args.installer.name: args.installer.read_bytes()},
                                        signing.file_signer(args.key)))
    else:
        server = UpdateServer(("127.0.0.1", args.port),
                              Offer(args.appid, args.version, args.crx, args.installer,
                                    args.arguments),
                              load_key(args.cup_key), args.log)
        print(f"serving {args.crx.name} as {args.version} on {server.base_url}", flush=True)
        server.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
