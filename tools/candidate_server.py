# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The update server repository's service on loopback, for the end-to-end test (sub-project E).

Runs ghost_update.service from a copy of project-ghost-update-server, with
one app's candidate release, and serves the packages under /releases/ as
Caddy does on the real server. It records every request body (for the
allow-list check) and every answer to the app's update checks.
"""

from __future__ import annotations

import functools
import hashlib
import http.server
import json
import os
import shutil
import sys
import threading
from pathlib import Path

ARGUMENTS = "--verbose-logging --do-not-launch-chrome"


class _Files(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path: str) -> str:
        prefix = "/releases/"
        if not path.startswith(prefix):
            return os.path.join(self.directory, "__not_served__")
        return super().translate_path("/" + path[len(prefix):])

    def log_message(self, format: str, *args) -> None:
        pass


class CandidateServer:
    def __init__(self, server_repo: Path, releases: Path, appid: str, version: str, crx: Path,
                 cup_key: Path, cup_version: int, log: Path, port: int = 8484,
                 file_port: int = 8485):
        if str(server_repo) not in sys.path:
            sys.path.insert(0, str(server_repo))
        from ghost_update import cup, identity, manifest, protocol, service
        self._manifest, self._protocol, self._identity = manifest, protocol, identity
        self.appid, self.releases, self.log = appid.lower(), releases, log
        self.answers: list[str] = []
        releases.mkdir(parents=True, exist_ok=True)
        name = f"{self.appid.strip('{}')}-{version}.crx3"
        shutil.copy(crx, releases / name)
        data = (releases / name).read_bytes()
        self.release = protocol.Release(version, name, len(data),
                                        hashlib.sha256(data).hexdigest(), "mini_installer.exe",
                                        ARGUMENTS)
        self.set_fraction(0.0)
        self._respond = protocol.respond
        protocol.respond = self._recording
        files = http.server.ThreadingHTTPServer(
            ("127.0.0.1", file_port), functools.partial(_Files, directory=str(releases)))
        self.service = service.Service(port, manifest.Store(releases),
                                       {cup_version: cup.load_key(cup_key)},
                                       f"http://127.0.0.1:{files.server_address[1]}")
        self.port = self.service.server_address[1]
        self._servers = [files, self.service]
        for server in self._servers:
            threading.Thread(target=server.serve_forever, daemon=True).start()

    def _recording(self, body: bytes, *args, **kwargs) -> bytes:
        payload = self._respond(body, *args, **kwargs)
        with open(self.log, "a", encoding="utf-8") as f:
            f.write(json.dumps({"body": json.loads(body)}) + "\n")
        prefix = len(self._protocol.RESPONSE_PREFIX)
        for app in json.loads(payload[prefix:])["response"]["apps"]:
            if app["appid"].lower() == self.appid and "updatecheck" in app:
                check = app["updatecheck"]
                self.answers.append(check.get("nextversion", check["status"]))
        return payload

    def set_fraction(self, fraction: float) -> None:
        m = self._manifest
        m.write(m.Manifest({
            self.appid: m.AppEntry(None, (), m.Candidate(self.release, fraction)),
            self._identity.UPDATER_APPID: m.AppEntry(None, ())}), self.releases)

    def shutdown(self) -> None:
        for server in self._servers:
            server.shutdown()
            server.server_close()
        self._protocol.respond = self._respond
