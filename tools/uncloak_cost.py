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
resolution's part of it. A NetLog, kept in the temporary profile (it holds IP
addresses), tells which resolutions the host cache answered: the filter's
requests are the network service's own (NETWORK_SERVICE_HOST_RESOLVER), one
per line, matched host by host in the order they started. A line that says
"remembered" had its aliases from the page's memo, with no resolution. The
budget is the second pass' requests the memo answered: p90 under 1 ms in an
official build. The first request to a host on a page, from the host cache or
the network, waits for a round trip to the network service and is reported,
with no budget. A host whose lines and requests don't pair up is left out and
counted. Live sites, so the numbers vary with the network: run it twice and
report both.

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
import urllib.parse
import urllib.request
from collections import defaultdict
from pathlib import Path
from typing import NamedTuple

import cdp

REPO = Path(__file__).resolve().parent.parent
CORPUS = REPO / "components" / "blocking" / "test" / "request_corpus.tsv"
# [pid:tid:date/time:LEVEL:file:line] message; the file's directories use
# either separator.
LINE = re.compile(r"^\[\d+:\d+:(\d\d)(\d\d)/(\d\d)(\d\d)(\d\d\.\d+):[^\]]*"
                  r"request_filter\.cc:\d+\] Uncloak (\S+): (\d+) us "
                  r"\((?:resolution (\d+) us|remembered)\)$")
BUDGET_US = 1000
# net::ERR_ABORTED: the filter cancelled the resolution, so it logged no line.
ERR_ABORTED = -3


class Uncloaking(NamedTuple):
    host: str
    end: float  # When its line was written, in seconds (the year left out).
    total_us: int
    # None when the page's memo had the host's aliases: nothing was resolved.
    resolution_us: int | None

    @property
    def start(self) -> float:
        return self.end - self.total_us / 1e6


def parse(log: str) -> list[Uncloaking]:
    """Each uncloaking in a chrome_debug.log text, in the order of its lines."""
    rows = []
    for m in map(LINE.search, log.splitlines()):
        if m:
            month, day, hours, minutes, seconds = m.group(1, 2, 3, 4, 5)
            days = int(month) * 31 + int(day)
            end = (days * 24 + int(hours)) * 3600 + int(minutes) * 60 + float(seconds)
            resolution = int(m.group(8)) if m.group(8) is not None else None
            rows.append(Uncloaking(m.group(6), end, int(m.group(7)), resolution))
    return rows


def cache_hits(netlog: dict) -> dict[str, list[bool]]:
    """For each host, whether the host cache answered each of the filter's
    resolutions, in the order they began. Speculative ones (the predictor's)
    and cancelled ones are left out."""
    constants = netlog["constants"]
    types = constants["logEventTypes"]
    phases = constants["logEventPhase"]
    ours = constants["logSourceType"]["NETWORK_SERVICE_HOST_RESOLVER"]
    begun: dict[int, tuple[float, str]] = {}
    hit: set[int] = set()
    ended: dict[int, int] = {}
    for event in netlog["events"]:
        source = event["source"]
        if source["type"] != ours:
            continue
        sid = source["id"]
        if event["type"] == types["HOST_RESOLVER_MANAGER_REQUEST"]:
            params = event.get("params", {})
            if event["phase"] == phases["PHASE_BEGIN"]:
                if not params.get("is_speculative"):
                    begun[sid] = (float(event["time"]), params["host"])
            elif event["phase"] == phases["PHASE_END"]:
                ended[sid] = params.get("net_error", 0)
        elif event["type"] == types["HOST_RESOLVER_MANAGER_CACHE_HIT"]:
            hit.add(sid)
    hosts: dict[str, list[bool]] = defaultdict(list)
    for sid, (_, host) in sorted(begun.items(), key=lambda item: (item[1][0], item[0])):
        if sid in ended and ended[sid] != ERR_ABORTED:
            hosts[urllib.parse.urlsplit(host).hostname or host].append(sid in hit)
    return dict(hosts)


def classify(rows: list[Uncloaking], hits: dict[str, list[bool]]) -> list[bool | None]:
    """Whether the host cache answered each row's resolution: rows and
    requests paired host by host in the order they started; None for a host
    whose counts differ, and for a row the memo answered (no resolution)."""
    by_host: dict[str, list[int]] = defaultdict(list)
    for index, row in enumerate(rows):
        if row.resolution_us is not None:
            by_host[row.host].append(index)
    result: list[bool | None] = [None] * len(rows)
    for host, indexes in by_host.items():
        answers = hits.get(host, [])
        if len(answers) != len(indexes):
            continue
        for index, answer in zip(sorted(indexes, key=lambda i: rows[i].start), answers):
            result[index] = answer
    return result


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
            extra: list[str]) -> tuple[list[Uncloaking], list[Uncloaking], list[bool | None]]:
    """Both passes' uncloakings, and for the two together (first, then
    second), whether the host cache answered each one."""
    with tempfile.TemporaryDirectory() as profile:
        log = Path(profile, "chrome_debug.log")
        netlog = Path(profile, "netlog.json")
        args = [str(chrome), f"--user-data-dir={profile}", "--no-first-run",
                "--no-default-browser-check", f"--remote-debugging-port={port}",
                "--enable-logging", "--vmodule=request_filter=2", f"--log-net-log={netlog}",
                *extra, "about:blank"]
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
        except BaseException:
            process.kill()
            raise
        # The browser writes the NetLog's end as it closes.
        hits = cache_hits(json.loads(netlog.read_text(encoding="utf-8")))
        return passes[0], passes[1], classify(passes[0] + passes[1], hits)


def _report(name: str, rows: list[Uncloaking]) -> str:
    totals = [row.total_us for row in rows]
    resolutions = [row.resolution_us for row in rows if row.resolution_us is not None]
    return (f"{name}: {len(rows)} requests, p50 {percentile(totals, 50)} us, "
            f"p90 {percentile(totals, 90)} us, p99 {percentile(totals, 99)} us; "
            f"resolution p50 {percentile(resolutions, 50)} us, "
            f"p99 {percentile(resolutions, 99)} us")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--chrome", type=Path, required=True)
    parser.add_argument("--launcher", type=Path)
    parser.add_argument("--port", type=int, default=9335)
    parser.add_argument("--wait", type=float, default=15)
    parser.add_argument("--chrome-arg", action="append", default=[])
    args = parser.parse_args(argv)
    first, second, cached = measure(args.chrome, args.launcher, args.port, args.wait,
                                    args.chrome_arg)
    print(_report("first", first))
    print(_report("second", second))
    second_cached = cached[len(first):]
    remembered = [row for row in second if row.resolution_us is None]
    hits = [row for row, hit in zip(second, second_cached) if hit is True]
    misses = [row for row, hit in zip(second, second_cached) if hit is False]
    print(_report("second, from the page's memo", remembered))
    print(_report("second, from the host cache", hits))
    print(_report("second, from the network", misses))
    resolved = [hit for row, hit in zip(first + second, cached) if row.resolution_us is not None]
    unmatched = sum(1 for hit in resolved if hit is None)
    print(f"unmatched: {unmatched} of {len(resolved)} resolutions (their host's lines and "
          "requests don't pair up)")
    p90 = percentile([row.total_us for row in remembered], 90)
    print(f"budget: second pass, from the page's memo, p90 under {BUDGET_US} us: "
          + ("no data" if p90 is None else "met" if p90 < BUDGET_US else "MISSED"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
