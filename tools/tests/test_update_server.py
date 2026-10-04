# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import datetime
import hashlib
import json
import re
import tempfile
import threading
import unittest
import urllib.request
from pathlib import Path

import crx3
import ecdsa_p256 as ec
import repo
import signing
import update_server as us

DATA = repo.REPO_ROOT / "test" / "updater"
APPID = "{33333333-3333-4333-8333-333333333333}"


def load(name: str) -> dict:
    return json.loads((DATA / name).read_text(encoding="utf-8"))


class AllowListTest(unittest.TestCase):
    def test_scrubbed_golden_is_allowed(self):
        self.assertEqual(us.disallowed_keys(load("request_scrubbed.json")), [])

    def test_every_dropped_key_is_reported(self):
        flagged = set(us.disallowed_keys(load("request_all_fields.json")))
        for path in ("request.dedup", "request.hw", "request.domainjoined", "request.updaters",
                     "request.futurekey", "request.os.sp", "request.apps[0].iid",
                     "request.apps[0].installdate", "request.apps[0].lang",
                     "request.apps[0].ping", "request.apps[0].events",
                     "request.apps[0].cohort", "request.apps[0].data[0].#text",
                     "request.apps[0].updatecheck.futurekey"):
            self.assertIn(path, flagged)

    def test_scrub_reference_matches_the_golden_output(self):
        # The C++ scrubber is tested against the same pair (Task 9).
        self.assertEqual(us.scrub(load("request_all_fields.json")), load("request_scrubbed.json"))


class CupTest(unittest.TestCase):
    def test_proof_verifies_like_cup_cc(self):
        key = us.load_key(us.CUP_KEY_FILE)
        proof = us.cup_proof(key, "1:42", b"request", b"response")
        self.assertTrue(us.cup_verify(ec.public_key(key), "1:42", b"request", b"response", proof))
        self.assertFalse(us.cup_verify(ec.public_key(key), "1:43", b"request", b"response", proof))
        self.assertEqual(proof.split(":")[1], hashlib.sha256(b"request").hexdigest())

    def test_vector_is_current(self):
        vector = load("cup_vector.json")
        key = us.load_key(us.CUP_KEY_FILE)
        self.assertEqual(us.cup_proof(key, vector["cup2key"], vector["request"].encode(),
                                      vector["response"].encode()), vector["proof"])


class KeyHeaderTest(unittest.TestCase):
    def test_the_development_header_is_current(self):
        keys = [signing.file_signer(f).public_der
                for f in (us.CUP_KEY_FILE, us.CRX_KEY_FILE, us.CRX_BACKUP_KEY_FILE)]
        self.assertEqual(us.DEV_HEADER.read_text(encoding="utf-8"),
                         signing.render_identity_header("dev", us.CUP_KEY_VERSION, keys[0],
                                                        keys[1:], "tools/update_server.py "
                                                        "dev-files"))


class IdentityTest(unittest.TestCase):
    def test_browser_appid_matches_the_install_mode(self):
        gni = (repo.REPO_ROOT / "branding" / "updater.gni").read_text(encoding="utf-8")
        modes = (repo.REPO_ROOT / "branding" / "install_modes.h").read_text(encoding="utf-8")
        appid = re.search(r'browser_appid = "([^"]+)"', gni).group(1)
        self.assertIn(f'.app_guid = L"{appid}"', modes)
        company = re.search(r'updater_company_short_name = "([^"]+)"', gni).group(1)
        self.assertIn(f'kCompanyPathName[] = L"{company}"', modes)


class FixtureTest(unittest.TestCase):
    def test_the_primary_fixture_is_signed_by_the_development_primary(self):
        data = (DATA / "data" / "ghost_publisher.crx3").read_bytes()
        self.assertEqual(crx3.verified_keys(data),
                         [signing.file_signer(us.CRX_KEY_FILE).public_der])

    def test_the_backup_fixture_carries_the_development_backup(self):
        data = (DATA / "data" / "ghost_backup_publisher.crx3").read_bytes()
        self.assertEqual(crx3.verified_keys(data),
                         [signing.file_signer(us.CRX_KEY_FILE).public_der,
                          signing.file_signer(us.CRX_BACKUP_KEY_FILE).public_der])

    def test_the_development_header_pins_the_committed_keys(self):
        pinned = signing.pinned_keys("dev")
        self.assertEqual(pinned.cup_version, 1)
        self.assertEqual(pinned.cup_der, signing.file_signer(us.CUP_KEY_FILE).public_der)
        self.assertEqual(pinned.publisher_hashes,
                         (signing.file_signer(us.CRX_KEY_FILE).key_hash,
                          signing.file_signer(us.CRX_BACKUP_KEY_FILE).key_hash))

    def test_the_vector_verifies_with_the_development_cup_key(self):
        vector = load("cup_vector.json")
        public = ec.public_key(us.load_key(us.CUP_KEY_FILE))
        self.assertTrue(us.cup_verify(public, vector["cup2key"], vector["request"].encode(),
                                      vector["response"].encode(), vector["proof"]))


class ResponseTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.crx = Path(tmp.name) / "update.crx3"
        self.crx.write_bytes(crx3.build({"mini_installer.exe": b"MZ"}, signing.scalar_signer("t", 1234)))
        self.offer = us.Offer(APPID, "152.0.7977.14902", self.crx, "mini_installer.exe",
                              "--verbose-logging --do-not-launch-chrome")

    def check(self, version: str) -> dict:
        request = {"request": {"apps": [{"appid": APPID, "version": version, "updatecheck": {}}]}}
        response = us.respond(request, self.offer, "http://127.0.0.1:8484",
                              datetime.date(2026, 10, 2))
        self.assertEqual(response["response"]["protocol"], "4.0")
        self.assertEqual(response["response"]["daystart"]["elapsed_days"], 7214)
        return response["response"]["apps"][0]

    def test_older_version_gets_the_crx(self):
        app = self.check("152.0.7977.14901")
        self.assertEqual(app["updatecheck"]["nextversion"], "152.0.7977.14902")
        download, install = app["updatecheck"]["pipelines"][0]["operations"]
        digest = hashlib.sha256(self.crx.read_bytes()).hexdigest()
        self.assertEqual(download["type"], "download")
        self.assertEqual(download["out"]["sha256"], digest)
        self.assertEqual(download["size"], self.crx.stat().st_size)
        self.assertEqual(download["urls"][0]["url"], "http://127.0.0.1:8484/download/update.crx3")
        self.assertEqual(install, {"type": "crx3", "in": {"sha256": digest},
                                   "path": "mini_installer.exe",
                                   "arguments": "--verbose-logging --do-not-launch-chrome"})

    def test_current_version_gets_noupdate(self):
        self.assertEqual(self.check("152.0.7977.14902")["updatecheck"], {"status": "noupdate"})

    def test_other_apps_get_noupdate(self):
        request = {"request": {"apps": [{"appid": "{other}", "version": "1.0.0.0",
                                         "updatecheck": {}}]}}
        app = us.respond(request, self.offer, "http://x", datetime.date(2026, 10, 2))
        self.assertEqual(app["response"]["apps"][0]["updatecheck"], {"status": "noupdate"})


class ServerTest(unittest.TestCase):
    def test_round_trip_with_cup_and_download(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        crx = Path(tmp.name) / "update.crx3"
        crx.write_bytes(crx3.build({"mini_installer.exe": b"MZ"}, signing.scalar_signer("t", 1234)))
        log = Path(tmp.name) / "requests.jsonl"
        key = us.load_key(us.CUP_KEY_FILE)
        server = us.UpdateServer(("127.0.0.1", 0), us.Offer(
            APPID, "152.0.7977.14902", crx, "mini_installer.exe", ""), key, log)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        base = f"http://127.0.0.1:{server.server_address[1]}"

        body = json.dumps({"request": {"apps": [{"appid": APPID, "version": "1.0.0.0",
                                                  "updatecheck": {}}]}}).encode()
        request = urllib.request.Request(base + "/update?cup2key=1:7&cup2hreq=x", data=body)
        with urllib.request.urlopen(request) as response:
            payload = response.read()
            proof = response.headers["X-Cup-Server-Proof"]
        self.assertTrue(payload.startswith(us.RESPONSE_PREFIX.encode()))
        self.assertTrue(us.cup_verify(ec.public_key(key), "1:7", body, payload, proof))
        self.assertEqual(json.loads(log.read_text(encoding="utf-8").splitlines()[0])["body"],
                         json.loads(body))

        ranged = urllib.request.Request(base + "/download/update.crx3",
                                        headers={"Range": "bytes=0-3"})
        with urllib.request.urlopen(ranged) as response:
            self.assertEqual(response.status, 206)
            self.assertEqual(response.read(), b"Cr24")


if __name__ == "__main__":
    unittest.main()
