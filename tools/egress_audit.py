#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Egress audit: which hosts does the browser contact on its own?

  run    launch a browser build on a fresh profile with a NetLog, leave it idle,
         load a local page, close it, then audit the log
  parse  audit an existing NetLog

A host outside test/egress/allowlist.json fails the audit. The allowlist is
empty in Phase 1; each future entry needs a written reason and review
(CONTRIBUTING.md).

NetLog records what goes through Chromium's network stack: page loads,
component and update checks, DNS, sockets. Crashpad uploads crash reports
through its own process and does not appear here; crash upload is disabled
in our builds.
"""

from __future__ import annotations

import argparse
import http.server
import json
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

import repo

ALLOWLIST = repo.REPO_ROOT / "test" / "egress" / "allowlist.json"

# Parameter names that carry a destination in some NetLog event. Matching on
# keys rather than event types keeps the audit working when a Chromium
# milestone adds or renames events.
_HOST_KEYS = {"url", "original_url", "host", "stream_key", "destination", "group_id",
              "server_id"}
_ADDRESS_KEYS = {"address", "remote_address", "address_list"}
_NON_NETWORK_SCHEMES = {"about", "blob", "chrome", "chrome-extension", "chrome-untrusted",
                        "data", "devtools", "file", "filesystem", "javascript"}
_LOOPBACK = {"localhost", "127.0.0.1", "::1"}
_URL_RE = re.compile(r"^([a-z][a-z0-9+.-]*)://(\[[^\]]+\]|[^/:?#\s]+)", re.IGNORECASE)
# A hostname needs a top-level label starting with a letter, so version-like
# tokens such as "1.0" are not mistaken for hosts; IPv4 needs four octets.
_HOST = r"(?:[a-z0-9-]+\.)+[a-z][a-z0-9-]*|\d{1,3}(?:\.\d{1,3}){3}|\[[0-9a-f:.]+\]"
_HOST_TOKEN_RE = re.compile(rf"^({_HOST})(?::\d+)?$", re.IGNORECASE)
_IP_PORT_RE = re.compile(r"^(\[[0-9a-f:.]+\]|\d{1,3}(?:\.\d{1,3}){3}):\d+$", re.IGNORECASE)


@dataclass
class Destination:
    host: str
    first_event: str
    count: int = 0
    examples: list[str] = field(default_factory=list)


def load_netlog(path: Path) -> dict:
    """Parses a NetLog, repairing the truncation left by an unclean exit."""
    text = path.read_text(encoding="utf-8", errors="replace")
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        pass
    # The file observer writes one event per line and closes the array on
    # exit; a killed browser leaves a trailing comma or a partial last line.
    cut = text.rfind("},\n")
    if cut < 0:
        raise ValueError(f"{path} is not a NetLog (no complete events)")
    return json.loads(text[:cut + 1] + "]}")


def host_from_string(value: str) -> str | None:
    """Returns the destination host named by a NetLog parameter value.

    Values come in several shapes: full URLs ("https://a.example:443/p"),
    host:port pairs, bare hosts, and composite keys such as
    "ssl/a.example:443" or "https://a.example:443 <NAK>".
    """
    value = value.strip()
    # Checked before anything else: blob:https://a.example/... embeds an
    # origin but never reaches the network.
    scheme = re.match(r"^([a-z][a-z0-9+.-]*):", value, re.IGNORECASE)
    if scheme and scheme.group(1).lower() in _NON_NETWORK_SCHEMES:
        return None
    m = _URL_RE.match(value)
    if m:
        return m.group(2).strip("[]").lower()
    for token in re.split(r"[\s/<>,]+", value):
        m = _HOST_TOKEN_RE.match(token)
        if m:
            return m.group(1).strip("[]").lower()
    return None


def _walk(value, keys: set[str]):
    """Yields string values stored under any of `keys`, at any depth."""
    if isinstance(value, dict):
        for k, v in value.items():
            if k in keys and isinstance(v, str):
                yield v
            elif k in keys and isinstance(v, list):
                yield from (x for x in v if isinstance(x, str))
            else:
                yield from _walk(v, keys)
    elif isinstance(value, list):
        for item in value:
            yield from _walk(item, keys)


def extract_destinations(netlog: dict) -> dict[str, Destination]:
    types = {v: k for k, v in netlog.get("constants", {}).get("logEventTypes", {}).items()}
    found: dict[str, Destination] = {}
    for event in netlog.get("events", []):
        params = event.get("params")
        if not params:
            continue
        name = types.get(event.get("type"), f"type {event.get('type')}")
        candidates = [(s, host_from_string(s)) for s in _walk(params, _HOST_KEYS)]
        for s in _walk(params, _ADDRESS_KEYS):
            m = _IP_PORT_RE.match(s)
            if m:
                candidates.append((s, m.group(1).strip("[]").lower()))
        for raw, host in candidates:
            if not host:
                continue
            if host in _LOOPBACK or host.startswith("127."):
                continue
            entry = found.setdefault(host, Destination(host, name))
            entry.count += 1
            if len(entry.examples) < 3 and raw not in entry.examples:
                entry.examples.append(raw)
    return found


def load_allowlist(path: Path = ALLOWLIST) -> list[dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    for entry in data["hosts"]:
        if not entry.get("reason"):
            raise ValueError(f"allowlist entry {entry.get('host')!r} has no reason")
        if entry.get("match", "exact") not in ("exact", "suffix"):
            raise ValueError(f"allowlist entry {entry['host']!r}: match must be exact or suffix")
    return data["hosts"]


def is_allowed(host: str, allowlist: list[dict]) -> bool:
    for entry in allowlist:
        pattern = entry["host"].lower()
        if host == pattern:
            return True
        # Suffix entries match whole labels only: example.com covers
        # a.example.com but never evilexample.com.
        if entry.get("match") == "suffix" and host.endswith("." + pattern):
            return True
    return False


def audit(netlog_path: Path, allowlist: list[dict]) -> tuple[list[Destination], list[Destination]]:
    found = extract_destinations(load_netlog(netlog_path))
    ordered = sorted(found.values(), key=lambda d: d.host)
    return ([d for d in ordered if is_allowed(d.host, allowlist)],
            [d for d in ordered if not is_allowed(d.host, allowlist)])


def format_report(allowed: list[Destination], unexpected: list[Destination]) -> str:
    lines = []
    for title, rows in (("UNEXPECTED", unexpected), ("allowed", allowed)):
        for d in rows:
            lines.append(f"{title:10} {d.host:45} x{d.count:<4} first seen in {d.first_event}")
            lines += [f"{'':10}   e.g. {example}" for example in d.examples]
    lines.append(f"{len(unexpected)} unexpected host(s), {len(allowed)} allowed.")
    return "\n".join(lines)


# --- Running the browser -----------------------------------------------------

def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class _QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve_page(directory: Path) -> tuple[http.server.ThreadingHTTPServer, str]:
    """Serves `directory` on loopback; the audit page lives there."""
    (directory / "index.html").write_text(
        "<!doctype html><title>egress audit</title><p>local page</p>", encoding="utf-8")
    handler = lambda *a, **kw: _QuietHandler(*a, directory=str(directory), **kw)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def browser_args(chrome: Path, profile: Path, netlog: Path, devtools_port: int,
                 headless: bool) -> list[str]:
    # Deliberately no --no-first-run and no flags that disable background
    # services: the point is to observe what a real first launch does.
    args = [str(chrome), f"--user-data-dir={profile}", f"--log-net-log={netlog}",
            "--net-log-capture-mode=Default", f"--remote-debugging-port={devtools_port}"]
    if headless:
        args.append("--headless")
    return args


def _devtools(port: int, path: str, method: str = "GET"):
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", method=method)
    with urllib.request.urlopen(req, timeout=5) as response:
        return json.loads(response.read() or b"null")


def run_browser(chrome: Path, idle_seconds: int, headless: bool, netlog: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="egress-profile-") as profile, \
         tempfile.TemporaryDirectory(prefix="egress-site-") as site:
        server, page_url = serve_page(Path(site))
        port = _free_port()
        proc = subprocess.Popen(browser_args(chrome, Path(profile), netlog, port, headless))
        try:
            deadline = time.monotonic() + 60
            while True:
                try:
                    _devtools(port, "/json/version")
                    break
                except OSError:
                    if time.monotonic() > deadline or proc.poll() is not None:
                        raise RuntimeError("browser did not expose DevTools within 60s")
                    time.sleep(1)
            print(f"idle for {idle_seconds}s ...", flush=True)
            time.sleep(idle_seconds)
            _devtools(port, f"/json/new?{page_url}", method="PUT")
            time.sleep(10)
            # Closing every page lets the browser shut down cleanly and
            # finish the NetLog; the parser copes if it does not.
            for target in _devtools(port, "/json/list"):
                if target.get("type") == "page":
                    try:
                        _devtools(port, f"/json/close/{target['id']}")
                    except OSError:
                        pass
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            pass
        finally:
            if proc.poll() is None:
                subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                               capture_output=True)
            server.shutdown()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--allowlist", type=Path, default=ALLOWLIST)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("parse")
    p.add_argument("netlog", type=Path)
    r = sub.add_parser("run")
    r.add_argument("--chrome", type=Path, required=True, help="path to chrome.exe")
    r.add_argument("--idle-seconds", type=int, default=600)
    r.add_argument("--headless", action="store_true")
    r.add_argument("--netlog", type=Path, help="where to keep the NetLog (default: temp)")
    args = parser.parse_args(argv)

    allowlist = load_allowlist(args.allowlist)
    if args.command == "run":
        netlog = args.netlog or Path(tempfile.mkdtemp(prefix="egress-")) / "netlog.json"
        run_browser(args.chrome, args.idle_seconds, args.headless, netlog)
        print(f"NetLog: {netlog}")
    else:
        netlog = args.netlog
    allowed, unexpected = audit(netlog, allowlist)
    print(format_report(allowed, unexpected))
    return 1 if unexpected else 0


if __name__ == "__main__":
    sys.exit(main())
