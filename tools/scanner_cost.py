#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Measures what cosmetic filtering costs a page's main thread.

    scanner_cost.py --chrome out/release/chrome.exe [--runs 5] [--launcher hidden.ps1]
                    [--site cosmetic.test] [--page heavy.html]

Loads a page of test/cosmetic/ (heavy.html: 5,000 elements at once), served
as http://<site>, in two fresh profiles: one at the default level, one with
the site set to Off, which inserts no rules and runs no script. Each load's
script and task time come from DevTools' Performance.getMetrics; the
difference of the medians is the cost.

The site is a registrable domain the lists name, so the page gets that
site's rules:
- Phase 4A: cosmetic.test (no site rules: the scanner), heavy.html; budget
  5 ms in an official build.
- Phase 4B: the site with the most scriptlets with light.html (budget 1 ms),
  and the site with the most procedural rules with heavy.html, less
  cosmetic.test's cost (budget 5 ms).

--launcher runs each browser through a script that takes -CommandLine (on the
development PC, the hidden-desktop launcher, so no window shows).
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

import cdp

REPO = Path(__file__).resolve().parent.parent
PAGES = REPO / "test" / "cosmetic"
METRICS = ("ScriptDuration", "TaskDuration")
# A name, not 127.0.0.1: EasyList exempts localhost and private addresses
# from generic hiding ($generichide), which would leave no scanner to measure.
SITE = "cosmetic.test"


def off_preferences(site: str) -> dict:
    """A profile's Preferences with the measured site at Off (3D-1's pref)."""
    return {"ghost": {"privacy_policy": {"site_levels": {site: "off"}}}}


def metric_deltas(before: dict[str, float], after: dict[str, float]) -> dict[str, float]:
    """What one load added to each metric, in milliseconds."""
    return {name: (after[name] - before[name]) * 1000 for name in METRICS}


def medians(rows: list[dict[str, float]]) -> dict[str, float]:
    return {name: statistics.median(row[name] for row in rows) for name in METRICS}


def command_line(args: list[str]) -> str:
    return subprocess.list2cmdline(args)


def _metrics(session: cdp.Session) -> dict[str, float]:
    return {m["name"]: m["value"] for m in session.call("Performance.getMetrics")["metrics"]}


def _wait_for(url: str, timeout: float = 60) -> object:
    deadline = time.monotonic() + timeout
    while True:
        try:
            with urllib.request.urlopen(url, timeout=5) as response:
                return json.load(response)
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.5)


def measure_profile(chrome: Path, site: str, page: str, runs: int, off: bool, port: int,
                    launcher: Path | None) -> list[dict[str, float]]:
    with tempfile.TemporaryDirectory() as profile:
        if off:
            default = Path(profile, "Default")
            default.mkdir()
            Path(default, "Preferences").write_text(json.dumps(off_preferences(site)),
                                                    encoding="utf-8")
        args = [str(chrome), f"--user-data-dir={profile}", "--no-first-run",
                "--no-default-browser-check", f"--remote-debugging-port={port}",
                f"--host-resolver-rules=MAP {site} 127.0.0.1", "about:blank"]
        if launcher:
            process = subprocess.Popen(["powershell", "-NoProfile", "-File", str(launcher),
                                        "-CommandLine", command_line(args)])
        else:
            process = subprocess.Popen(args)
        try:
            version = _wait_for(f"http://127.0.0.1:{port}/json/version")
            targets = _wait_for(f"http://127.0.0.1:{port}/json/list")
            target = next(t for t in targets if t["type"] == "page")
            session = cdp.Session.connect(target["webSocketDebuggerUrl"])
            session.call("Performance.enable")
            rows = []
            for _ in range(runs):
                session.call("Page.navigate", url="about:blank")
                time.sleep(0.5)
                before = _metrics(session)
                session.call("Page.navigate", url=page)
                for _ in range(100):
                    state = session.call("Runtime.evaluate", expression="document.readyState")
                    if state["result"].get("value") == "complete":
                        break
                    time.sleep(0.1)
                time.sleep(1.0)  # the scanner's later batches
                rows.append(metric_deltas(before, _metrics(session)))
            session.close()
            browser = cdp.Session.connect(version["webSocketDebuggerUrl"])
            try:
                browser.call("Browser.close")
            except (cdp.ProtocolError, OSError):
                pass
            process.wait(timeout=60)
            return rows
        except BaseException:
            process.kill()
            raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--chrome", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--launcher", type=Path)
    parser.add_argument("--port", type=int, default=9334)
    parser.add_argument("--site", default=SITE)
    parser.add_argument("--page", default="heavy.html")
    args = parser.parse_args(argv)

    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(PAGES))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    page = f"http://{args.site}:{server.server_address[1]}/{args.page}"
    try:
        results = {}
        for name, off in (("on", False), ("off", True)):
            rows = measure_profile(args.chrome, args.site, page, args.runs, off, args.port,
                                   args.launcher)
            results[name] = medians(rows)
            print(f"{name:>3}: " + ", ".join(f"{k} {v:.1f} ms" for k, v in results[name].items()))
        cost = {k: results["on"][k] - results["off"][k] for k in METRICS}
        print("cost: " + ", ".join(f"{k} {v:.1f} ms" for k, v in cost.items()))
    finally:
        server.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
