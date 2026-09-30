#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Egress audit: which hosts does the browser contact on its own?

  run    launch a browser build on a fresh profile with a NetLog, leave it idle,
         load a local page, act out the scenarios, close it, then audit the log
  parse  audit an existing NetLog

The scenarios do what users do on a site, on pages served from loopback:
"address" fills in and submits a shipping address, "login" signs in with a
username and password. Some senders only start after such actions; Autofill's
form classification did.

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
import html.parser
import http.server
import json
import re
import secrets
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

import cdp
import repo

ALLOWLIST = repo.REPO_ROOT / "test" / "egress" / "allowlist.json"
SITE = repo.REPO_ROOT / "test" / "egress" / "site"

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
# A single-label name counts only with a port ("wpad:80"): bare, it is
# indistinguishable from words such as "GET".
_SINGLE_LABEL_RE = re.compile(r"^([a-z][a-z0-9-]*):\d+$", re.IGNORECASE)
_IP_PORT_RE = re.compile(r"^(\[[0-9a-f:.]+\]|\d{1,3}(?:\.\d{1,3}){3}):\d+$", re.IGNORECASE)
# Logged on the UDP_SOCKET source whenever a datagram is sent or received, in
# every capture mode.
_UDP_DATA_EVENTS = {"UDP_BYTES_SENT", "UDP_BYTES_RECEIVED", "UDP_SEND_ERROR",
                    "UDP_RECEIVE_ERROR"}


@dataclass
class Destination:
    host: str
    first_event: str
    # "host": a name, or an IP a name was resolved to in this log.
    # "ip": an address connected to without any resolution in the log.
    # "dns": the system's DNS resolver (port 53); reported, never a failure.
    # "probe": a UDP socket connected to find a route, with nothing sent;
    #          reported, never a failure.
    # "proxy": looked up only to auto-detect a proxy (WPAD), as the system's
    #          proxy settings ask; reported, never a failure.
    kind: str = "host"
    count: int = 0
    examples: list[str] = field(default_factory=list)
    # Unix time in ms of the first event, when the log says how to convert.
    first_time: float | None = None


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
        m = _HOST_TOKEN_RE.match(token) or _SINGLE_LABEL_RE.match(token)
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


def resolved_addresses(netlog: dict) -> dict[str, str]:
    """Maps each IP that DNS returned in this log to the name it was resolved from.

    Two shapes occur: fresh results ({"domain_name", "endpoints": [{"address"}]})
    and cache hits ({"aliases", "ip_endpoints": [{"endpoint_address"}]}).
    """
    names: dict[str, str] = {}

    def visit(value):
        if isinstance(value, dict):
            if isinstance(value.get("domain_name"), str):
                for ep in value.get("endpoints") or []:
                    if isinstance(ep, dict) and ep.get("address"):
                        names.setdefault(ep["address"].lower(), value["domain_name"].lower())
            aliases = value.get("aliases")
            if aliases and isinstance(aliases, list) and isinstance(aliases[0], str):
                for ep in value.get("ip_endpoints") or []:
                    if isinstance(ep, dict) and ep.get("endpoint_address"):
                        names.setdefault(ep["endpoint_address"].lower(), aliases[0].lower())
            for v in value.values():
                visit(v)
        elif isinstance(value, list):
            for v in value:
                visit(v)

    for event in netlog.get("events", []):
        visit(event.get("params"))
    return names


def udp_probe_sources(netlog: dict) -> set[int]:
    """Returns the ids of UDP sockets that connected but never carried a datagram.

    Connecting a UDP socket sends nothing: the OS only picks a route. Chromium
    probes reachability that way; HostResolverManager's IPv6 probe connects to
    [2001:4860:4860::8888]:443 and closes the socket. Datagrams are logged on
    the UDP_SOCKET source, which names the UDP_CLIENT_SOCKET wrapping it as its
    source_dependency, so a wrapper carried data if its socket did.
    """
    constants = netlog.get("constants", {})
    types = {v: k for k, v in constants.get("logEventTypes", {}).items()}
    source_types = {v: k for k, v in constants.get("logSourceType", {}).items()}
    udp: set[int] = set()
    carried_data: set[int] = set()
    owner: dict[int, int] = {}
    for event in netlog.get("events", []):
        source = event.get("source", {})
        if "UDP" not in source_types.get(source.get("type"), ""):
            continue
        udp.add(source.get("id"))
        if types.get(event.get("type")) in _UDP_DATA_EVENTS:
            carried_data.add(source.get("id"))
        dependency = (event.get("params") or {}).get("source_dependency")
        if isinstance(dependency, dict) and "id" in dependency:
            owner[source.get("id")] = dependency["id"]
    carried_data |= {owner[s] for s in carried_data if s in owner}
    return udp - carried_data


def proxy_detection_sources(netlog: dict) -> set[int]:
    """Returns the ids of the PAC file deciders and of the resolver jobs they started.

    With "Automatically detect settings" on in Windows, Chromium looks for a
    proxy configuration by resolving the name wpad, as the system does.
    """
    source_types = {v: k for k, v in netlog.get("constants", {}).get("logSourceType", {}).items()}
    deciders: set[int] = set()
    started_by: dict[int, int] = {}
    for event in netlog.get("events", []):
        source = event.get("source", {})
        if source_types.get(source.get("type")) == "PAC_FILE_DECIDER":
            deciders.add(source.get("id"))
        dependency = (event.get("params") or {}).get("source_dependency")
        if isinstance(dependency, dict) and "id" in dependency:
            started_by[source.get("id")] = dependency["id"]
    return deciders | {s for s, parent in started_by.items() if parent in deciders}


def _event_time(event: dict, tick_offset) -> float | None:
    """Converts an event's TimeTicks (ms, as a string) to Unix time in ms."""
    try:
        return float(event["time"]) + float(tick_offset)
    except (KeyError, TypeError, ValueError):
        return None


def extract_destinations(netlog: dict) -> dict[str, Destination]:
    constants = netlog.get("constants", {})
    types = {v: k for k, v in constants.get("logEventTypes", {}).items()}
    tick_offset = constants.get("timeTickOffset")
    by_ip = resolved_addresses(netlog)
    probes = udp_probe_sources(netlog)
    proxy_detection = proxy_detection_sources(netlog)
    found: dict[str, Destination] = {}

    def record(key: str, kind: str, event_name: str, raw: str) -> None:
        if key not in found:
            found[key] = Destination(key, event_name, kind,
                                     first_time=_event_time(event, tick_offset))
        entry = found[key]
        # Contacted for anything besides proxy detection: that is traffic.
        if entry.kind == "proxy" and kind == "host":
            entry.kind = "host"
        entry.count += 1
        if len(entry.examples) < 3 and raw not in entry.examples:
            entry.examples.append(raw)

    for event in netlog.get("events", []):
        params = event.get("params")
        name = types.get(event.get("type"), f"type {event.get('type')}")
        # *_LOCAL_ADDRESS events carry this machine's own addresses.
        if not params or "LOCAL_ADDRESS" in name:
            continue
        in_proxy_detection = event.get("source", {}).get("id") in proxy_detection
        for raw in _walk(params, _HOST_KEYS):
            host = host_from_string(raw)
            if host and host not in _LOOPBACK and not host.startswith("127."):
                record(host, "proxy" if in_proxy_detection else "host", name, raw)
        # Addresses count as destinations only in connect events; elsewhere
        # (DNS results, socket bookkeeping) they are not connections.
        if "CONNECT" not in name:
            continue
        for raw in _walk(params, _ADDRESS_KEYS):
            m = _IP_PORT_RE.match(raw)
            if not m:
                continue
            ip = m.group(1).strip("[]").lower()
            if ip in _LOOPBACK or ip.startswith("127."):
                continue
            if event.get("source", {}).get("id") in probes:
                record(ip, "probe", name, raw)
            elif raw.endswith(":53"):
                record(ip, "dns", name, raw)
            elif ip in by_ip:
                record(by_ip[ip], "host", name, raw)
            else:
                record(ip, "ip", name, raw)
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


@dataclass
class AuditResult:
    unexpected: list[Destination]
    allowed: list[Destination]
    dns: list[Destination]
    probes: list[Destination]
    proxy: list[Destination]


def audit(netlog_path: Path, allowlist: list[dict]) -> AuditResult:
    found = extract_destinations(load_netlog(netlog_path))
    ordered = sorted(found.values(), key=lambda d: (d.kind != "host", d.host))
    checked = [d for d in ordered if d.kind not in ("dns", "probe", "proxy")]
    return AuditResult(
        unexpected=[d for d in checked if not is_allowed(d.host, allowlist)],
        allowed=[d for d in checked if is_allowed(d.host, allowlist)],
        dns=[d for d in ordered if d.kind == "dns"],
        probes=[d for d in ordered if d.kind == "probe"],
        proxy=[d for d in ordered if d.kind == "proxy"])


def phase_at(when: float | None, phases: list[tuple[str, float]]) -> str | None:
    """Names the phase running at `when`; phases are (name, start in Unix ms), in order."""
    current = None
    for name, start in phases:
        if when is None or when < start:
            break
        current = name
    return current


def format_report(result: AuditResult, phases: list[tuple[str, float]] | None = None) -> str:
    lines = []
    for title, rows in (("UNEXPECTED", result.unexpected), ("allowed", result.allowed),
                        ("dns", result.dns), ("probe", result.probes), ("proxy", result.proxy)):
        for d in rows:
            label = d.host if d.kind != "ip" else f"{d.host} (raw IP, never resolved)"
            phase = phase_at(d.first_time, phases or [])
            during = f" during {phase}" if phase else ""
            lines.append(f"{title:10} {label:45} x{d.count:<4} first seen in {d.first_event}"
                         f"{during}")
            lines += [f"{'':10}   e.g. {example}" for example in d.examples]
    lines.append(f"{len(result.unexpected)} unexpected, {len(result.allowed)} allowed; "
                 f"not counted: {len(result.dns)} DNS resolver(s), {len(result.probes)} route "
                 f"probe(s), {len(result.proxy)} proxy auto-detection lookup(s).")
    return "\n".join(lines)


# --- Running the browser -----------------------------------------------------

def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class _SiteHandler(http.server.SimpleHTTPRequestHandler):
    """Serves the audit site; any form submission lands on a page without a form."""

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length") or 0))
        body = b"<!doctype html><meta charset=utf-8><title>Done</title><p>Done."
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def serve_site(directory: Path = SITE) -> tuple[http.server.ThreadingHTTPServer, str]:
    """Serves the audit pages on loopback."""
    handler = lambda *a, **kw: _SiteHandler(*a, directory=str(directory), **kw)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


@dataclass
class FormScenario:
    name: str
    page: str
    # (input id, text typed into it), in page order; Enter in the last field submits.
    fields: list[tuple[str, str]]


def form_scenarios(password: str) -> list[FormScenario]:
    # Made-up values: example.com mail, a 555-01xx number reserved for fiction.
    return [
        FormScenario("address", "address.html", [
            ("name", "Ana Test"), ("email", "ana.test@example.com"), ("tel", "(217) 555-0100"),
            ("street", "100 Example Street"), ("city", "Springfield"), ("state", "IL"),
            ("zip", "62701"), ("country", "United States")]),
        FormScenario("login", "login.html", [("username", "ghost-audit"), ("password", password)]),
    ]


SCENARIO_NAMES = [s.name for s in form_scenarios(password="")]


def parse_scenarios(value: str) -> list[str]:
    names = SCENARIO_NAMES if value == "all" else [n for n in value.split(",") if n]
    unknown = [n for n in names if n not in SCENARIO_NAMES]
    if unknown:
        raise argparse.ArgumentTypeError(
            f"unknown scenario(s) {', '.join(unknown)}; choose from {', '.join(SCENARIO_NAMES)}")
    return names


def key_events(text: str) -> list[dict]:
    """Input.dispatchKeyEvent parameters that type `text`, one key press per character."""
    events = []
    for ch in text:
        events.append({"type": "keyDown", "key": ch, "text": ch, "unmodifiedText": ch})
        events.append({"type": "keyUp", "key": ch})
    return events


ENTER = [{"type": "keyDown", "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13,
          "text": "\r", "unmodifiedText": "\r"},
         {"type": "keyUp", "key": "Enter", "code": "Enter", "windowsVirtualKeyCode": 13}]


def browser_args(chrome: Path, profile: Path, netlog: Path, devtools_port: int,
                 headless: bool) -> list[str]:
    # Deliberately no --no-first-run and no flags that disable background
    # services: the point is to observe what a real first launch does.
    args = [str(chrome), f"--user-data-dir={profile}", f"--log-net-log={netlog}",
            "--net-log-capture-mode=Default", f"--remote-debugging-port={devtools_port}"]
    if headless:
        args.append("--headless")
    return args


def _devtools(port: int, path: str, method: str = "GET", parse: bool = True):
    # /json/close answers with plain text ("Target is closing"), not JSON.
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", method=method)
    with urllib.request.urlopen(req, timeout=5) as response:
        body = response.read()
    return json.loads(body or b"null") if parse else body.decode("utf-8", "replace")


def _evaluate(session: cdp.Session, expression: str):
    reply = session.call("Runtime.evaluate", expression=expression, returnByValue=True)
    if "exceptionDetails" in reply:
        raise cdp.ProtocolError(f"{expression}: {reply['exceptionDetails'].get('text')}")
    return reply["result"].get("value")


def _wait_for(session: cdp.Session, expression: str, timeout: float = 30) -> None:
    deadline = time.monotonic() + timeout
    while True:
        try:
            if _evaluate(session, expression):
                return
        except cdp.ProtocolError:
            pass  # the page is between documents
        if time.monotonic() > deadline:
            raise TimeoutError(f"{expression} not true within {timeout}s")
        time.sleep(0.2)


def _click(session: cdp.Session, element_id: str) -> None:
    x, y = _evaluate(session, f"(() => {{ const r = document.getElementById("
                              f"{json.dumps(element_id)}).getBoundingClientRect();"
                              f" return [r.x + r.width / 2, r.y + r.height / 2]; }})()")
    for kind in ("mousePressed", "mouseReleased"):
        session.call("Input.dispatchMouseEvent", type=kind, x=x, y=y, button="left",
                     clickCount=1)


def run_form_scenario(devtools_port: int, site_url: str, scenario: FormScenario,
                      settle_seconds: int) -> None:
    """Opens the scenario's page in a new tab and fills it in with real key presses.

    Clicks and key presses go through the input pipeline, so Autofill and the
    password manager see a user typing, as they would not for values set by
    script.
    """
    target = _devtools(devtools_port, "/json/new?about:blank", method="PUT")
    session = cdp.Session.connect(target["webSocketDebuggerUrl"])
    page_path = json.dumps("/" + scenario.page)
    try:
        session.call("Page.navigate", url=site_url + scenario.page)
        _wait_for(session, f"location.pathname === {page_path}"
                           " && document.readyState === 'complete'")
        for element_id, text in scenario.fields:
            _click(session, element_id)
            for key in key_events(text):
                session.call("Input.dispatchKeyEvent", **key)
        for key in ENTER:
            session.call("Input.dispatchKeyEvent", **key)
        _wait_for(session, f"location.pathname !== {page_path}"
                           " && document.readyState === 'complete'")
        time.sleep(settle_seconds)
    finally:
        session.close()


def run_browser(chrome: Path, idle_seconds: int, headless: bool, netlog: Path,
                scenarios: list[str], settle_seconds: int = 15) -> list[tuple[str, float]]:
    """Runs the browser through its phases; returns each phase's start in Unix ms."""
    phases: list[tuple[str, float]] = []

    def begin(name: str) -> None:
        print(f"{name} ...", flush=True)
        phases.append((name, time.time() * 1000))

    # A fresh password per run: the leak check, where it runs, hashes it.
    chosen = [s for s in form_scenarios(secrets.token_urlsafe(12)) if s.name in scenarios]
    with tempfile.TemporaryDirectory(prefix="egress-profile-") as profile:
        server, site_url = serve_site()
        port = _free_port()
        begin("startup")
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
            begin("idle")
            time.sleep(idle_seconds)
            begin("local page")
            _devtools(port, f"/json/new?{site_url}", method="PUT")
            time.sleep(10)
            for scenario in chosen:
                begin(scenario.name)
                run_form_scenario(port, site_url, scenario, settle_seconds)
            begin("shutdown")
            # Closing every page lets the browser shut down cleanly and
            # finish the NetLog; the parser copes if it does not.
            for target in _devtools(port, "/json/list"):
                if target.get("type") == "page":
                    try:
                        _devtools(port, f"/json/close/{target['id']}", parse=False)
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
            server.server_close()
    return phases


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
    r.add_argument("--scenarios", type=parse_scenarios, default=SCENARIO_NAMES,
                   help=f"comma-separated, or all (default): {', '.join(SCENARIO_NAMES)}")
    args = parser.parse_args(argv)

    allowlist = load_allowlist(args.allowlist)
    phases: list[tuple[str, float]] = []
    if args.command == "run":
        netlog = args.netlog or Path(tempfile.mkdtemp(prefix="egress-")) / "netlog.json"
        phases = run_browser(args.chrome, args.idle_seconds, args.headless, netlog,
                             args.scenarios)
        print(f"NetLog: {netlog}")
    else:
        netlog = args.netlog
    result = audit(netlog, allowlist)
    print(format_report(result, phases))
    return 1 if result.unexpected else 0


if __name__ == "__main__":
    sys.exit(main())
