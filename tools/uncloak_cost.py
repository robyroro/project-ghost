#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Measures what CNAME uncloaking adds to requests (Phase 4C).

    uncloak_cost.py --chrome out/release/chrome.exe [--launcher hidden.ps1] [--wait 15]
                    [--chrome-arg=--no-proxy-server ...]

Loads the perftest corpus' 20 front pages twice in one fresh profile, with the
request filter's timing lines on (--vmodule=request_filter=2): each line is
the time from a request's resolution to its aliases' verdict, and the
resolution's part of it. The first pass fills the host cache; the second is
the budget's (p99 under 1 ms in an official build). Live sites, so the numbers vary with the network: run it
twice and report both.

--chrome-arg passes a switch to the browser (repeatable), to compare
configurations: --no-proxy-server takes the proxy check out of the
resolution.

--launcher runs the browser through a script that takes -CommandLine (on the
development PC, the hidden-desktop launcher, so no window shows).
"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

import cdp

REPO = Path(__file__).resolve().parent.parent
CORPUS = REPO / "components" / "blocking" / "test" / "request_corpus.tsv"
# [pid:tid:date/time:LEVEL:file:line] message; the file's directories use
# either separator.
LINE = re.compile(r"request_filter\.cc:\d+\] Uncloak \S+: (\d+) us \(resolution (\d+) us\)$")
BUDGET_US = 1000


def parse(log: str) -> list[tuple[int, int]]:
    """Each uncloaking's microseconds in a chrome_debug.log text: the total
    and the resolution's part."""
    return [(int(m.group(1)), int(m.group(2)))
            for m in map(LINE.search, log.splitlines()) if m]


def percentile(values: list[int], p: float) -> int | None:
    """The nearest-rank percentile; None for no values."""
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(0, math.ceil(p / 100 * len(ordered)) - 1)]


def corpus_pages() -> list[str]:
    pages = {line.split("\t")[1] for line in CORPUS.read_text(encoding="utf-8").splitlines()
             if line.strip()}
    return sorted(pages)


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


def measure(chrome: Path, launcher: Path | None, port: int, wait: float,
            extra: list[str]) -> tuple[list[tuple[int, int]], list[tuple[int, int]]]:
    with tempfile.TemporaryDirectory() as profile:
        log = Path(profile, "chrome_debug.log")
        args = [str(chrome), f"--user-data-dir={profile}", "--no-first-run",
                "--no-default-browser-check", f"--remote-debugging-port={port}",
                "--enable-logging", "--vmodule=request_filter=2", *extra, "about:blank"]
        if launcher:
            process = subprocess.Popen(["powershell", "-NoProfile", "-File", str(launcher),
                                        "-CommandLine", subprocess.list2cmdline(args)])
        else:
            process = subprocess.Popen(args)
        try:
            version = _wait_for(f"http://127.0.0.1:{port}/json/version")
            target = next(t for t in _wait_for(f"http://127.0.0.1:{port}/json/list")
                          if t["type"] == "page")
            session = cdp.Session.connect(target["webSocketDebuggerUrl"])
            passes = []
            for _ in range(2):
                offset = log.stat().st_size if log.exists() else 0
                for page in corpus_pages():
                    session.call("Page.navigate", url=page)
                    time.sleep(wait)
                with log.open(encoding="utf-8", errors="replace") as f:
                    f.seek(offset)
                    passes.append(parse(f.read()))
            session.close()
            browser = cdp.Session.connect(version["webSocketDebuggerUrl"])
            try:
                browser.call("Browser.close")
            except (cdp.ProtocolError, OSError):
                pass
            process.wait(timeout=60)
            return passes[0], passes[1]
        except BaseException:
            process.kill()
            raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--chrome", type=Path, required=True)
    parser.add_argument("--launcher", type=Path)
    parser.add_argument("--port", type=int, default=9335)
    parser.add_argument("--wait", type=float, default=15)
    parser.add_argument("--chrome-arg", action="append", default=[])
    args = parser.parse_args(argv)
    first, second = measure(args.chrome, args.launcher, args.port, args.wait, args.chrome_arg)
    for name, rows in (("first", first), ("second", second)):
        totals = [total for total, _ in rows]
        resolutions = [resolution for _, resolution in rows]
        print(f"{name}: {len(rows)} requests, p50 {percentile(totals, 50)} us, "
              f"p99 {percentile(totals, 99)} us; resolution p50 "
              f"{percentile(resolutions, 50)} us, p99 {percentile(resolutions, 99)} us")
    p99 = percentile([total for total, _ in second], 99)
    print(f"budget: second pass p99 under {BUDGET_US} us: "
          + ("no data" if p99 is None else "met" if p99 < BUDGET_US else "MISSED"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
