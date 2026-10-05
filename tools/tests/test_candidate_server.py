# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import os
import tempfile
import unittest
import urllib.request
from pathlib import Path

import candidate_server
import repo
import update_server

SERVER_REPO = Path(os.environ.get("GHOST_UPDATE_SERVER",
                                  repo.REPO_ROOT.parent / "project-ghost-update-server"))
APPID = "{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}"


@unittest.skipUnless((SERVER_REPO / "ghost_update" / "service.py").is_file(),
                     "needs project-ghost-update-server beside this repository")
class CandidateServerTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = Path(tmp.name)
        (self.tmp / "update.crx3").write_bytes(b"package")
        self.server = candidate_server.CandidateServer(
            SERVER_REPO, self.tmp / "releases", APPID, "152.0.7977.14902",
            self.tmp / "update.crx3", update_server.CUP_KEY_FILE, 1, self.tmp / "log.jsonl",
            port=0, file_port=0)
        self.addCleanup(self.server.shutdown)

    def check(self) -> dict:
        body = json.dumps({"request": {"protocol": "4.0", "apps": [
            {"appid": APPID, "version": "152.0.7977.14901", "updatecheck": {}}]}}).encode()
        url = f"http://127.0.0.1:{self.server.port}/update?cup2key=1:123"
        with urllib.request.urlopen(urllib.request.Request(url, body)) as r:
            payload = r.read()
        return json.loads(payload[5:])["response"]["apps"][0]["updatecheck"]

    def test_fraction_zero_offers_nothing(self):
        self.assertEqual(self.check(), {"status": "noupdate"})
        self.assertEqual(self.server.answers, ["noupdate"])

    def test_fraction_one_offers_and_serves_the_candidate(self):
        self.server.set_fraction(1.0)
        check = self.check()
        self.assertEqual(check["nextversion"], "152.0.7977.14902")
        url = check["pipelines"][0]["operations"][0]["urls"][0]["url"]
        with urllib.request.urlopen(url) as r:
            self.assertEqual(r.read(), b"package")
        self.assertEqual(self.server.answers, ["152.0.7977.14902"])

    def test_requests_are_logged_for_the_allow_list(self):
        self.check()
        [line] = (self.tmp / "log.jsonl").read_text().splitlines()
        self.assertEqual(json.loads(line)["body"]["request"]["apps"][0]["appid"], APPID)
