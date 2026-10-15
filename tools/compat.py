#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The compatibility corpus (Phase 4D-1): what Shade breaks that Chromium doesn't.

    compat.py baseline [--dir DIR]
    compat.py run --shade <exe> --baseline <exe> [--launcher hidden.ps1]
                  [--level standard|strict|off ...] [--sites URL ...]
                  [--category C ...] [--self-test] [--out report.json]
    compat.py report report.json

baseline fetches Chrome for Testing at the pinned version (CHROMIUM_VERSION)
from Google's official storage, checks its SHA-256 against
test/compat/baseline.json (recording it for a new pin: commit that), unpacks
it outside the repository and prints its chrome.exe.

run loads every site of test/compat/corpus.toml in Shade and in the baseline
at once, each browser on a fresh profile, Shade at the given level (its
default level; off for triage), and judges Shade against the baseline
(compat_verdict.py). Standard and Strict by default. A broken site is run
once more. --self-test runs test/compat/site/ instead, served on loopback:
the page whose script EasyPrivacy blocks must be broken, the other passed.

--launcher runs each browser through a script that takes -CommandLine (on the
development PC, the hidden-desktop launcher, so no window shows).
"""

from __future__ import annotations

import argparse
import base64
import concurrent.futures
import functools
import hashlib
import http.server
import json
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import zipfile
from pathlib import Path
from typing import Callable

import cdp
import compat_corpus
import compat_page
import compat_verdict as v
from compat_corpus import Site, Step
from compat_verdict import Signals

REPO = Path(__file__).resolve().parent.parent
RECORD = REPO / "test" / "compat" / "baseline.json"
SITE_DIR = REPO / "test" / "compat" / "site"
CFT = "https://storage.googleapis.com/chrome-for-testing-public/{version}/win64/chrome-win64.zip"
DEFAULT_DIR = Path.home() / "shade-compat"
# The self-test's hosts: the page's, and a third party's whose script
# EasyPrivacy's generic /adobe-analytics.js rule blocks.
SELF_TEST_HOSTS = ("compat.test", "tracker.compat-other.test")

Visit = Callable[[Site], Signals]


class BaselineError(RuntimeError):
    """The baseline's download isn't the one recorded."""


def _download(url: str, path: Path) -> None:
    with urllib.request.urlopen(url, timeout=60) as response, open(path, "wb") as out:
        shutil.copyfileobj(response, out, 1 << 20)


def fetch_baseline(version: str, directory: Path, record: Path = RECORD,
                   download: Callable[[str, Path], None] = _download) -> Path:
    """chrome.exe of Chrome for Testing `version`, unpacked under `directory`."""
    exe = directory / version / "chrome-win64" / "chrome.exe"
    if exe.is_file():
        return exe
    directory.mkdir(parents=True, exist_ok=True)
    url = CFT.format(version=version)
    archive = directory / f"chrome-win64-{version}.zip"
    download(url, archive)
    data = archive.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    known = json.loads(record.read_text(encoding="utf-8")) if record.is_file() else {}
    if known.get("version") == version:
        if known.get("sha256") != digest:
            archive.unlink()
            raise BaselineError(f"{url}: SHA-256 {digest}, recorded {known.get('sha256')}")
    else:
        record.write_bytes((json.dumps({"version": version, "sha256": digest, "url": url},
                                       indent=1) + "\n").encode("utf-8"))
        # Google's storage answers a HEAD with the object's MD5 (x-goog-hash):
        # compare it once, by hand, before trusting the first record.
        md5 = base64.b64encode(hashlib.md5(data).digest()).decode("ascii")
        print(f"recorded {version}'s SHA-256 in {record}: commit it. MD5 {md5}: check it "
              f"against `curl -sI {url}`'s x-goog-hash")
    with zipfile.ZipFile(archive) as z:
        z.extractall(directory / version)
    archive.unlink()
    return exe


def preferences(level: str) -> dict:
    """Shade's profile at `level` for every site (3D-4's default level)."""
    return {"ghost": {"privacy_policy": {"default_level": level}}}


def browser_args(exe: Path, profile: Path, port: int, extra: list[str]) -> list[str]:
    return [str(exe), f"--user-data-dir={profile}", "--no-first-run",
            "--no-default-browser-check", f"--remote-debugging-port={port}", *extra,
            "about:blank"]


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _devtools(port: int, path: str, method: str = "GET"):
    request = urllib.request.Request(f"http://127.0.0.1:{port}{path}", method=method)
    with urllib.request.urlopen(request, timeout=10) as response:
        body = response.read()
    try:
        return json.loads(body or b"null")
    except ValueError:
        return body.decode("utf-8", "replace")  # /json/close answers in plain text


class Browser:
    """One browser on a fresh profile, driven over DevTools."""

    def __init__(self, exe: Path, launcher: Path | None, prefs: dict | None,
                 extra: list[str]):
        self.exe, self.launcher, self.prefs, self.extra = exe, launcher, prefs, extra
        self.process = None
        self.profile = None
        self.port = 0

    def start(self) -> None:
        self.profile = Path(tempfile.mkdtemp(prefix="compat-profile-"))
        if self.prefs:
            (self.profile / "Default").mkdir()
            (self.profile / "Default" / "Preferences").write_bytes(
                json.dumps(self.prefs).encode("utf-8"))
        self.port = _free_port()
        args = browser_args(self.exe, self.profile, self.port, self.extra)
        if self.launcher:
            args = ["powershell", "-NoProfile", "-File", str(self.launcher),
                    "-CommandLine", subprocess.list2cmdline(args)]
        self.process = subprocess.Popen(args)
        deadline = time.monotonic() + 60
        while not self.alive():
            if time.monotonic() > deadline:
                raise RuntimeError(f"{self.exe} exposed no DevTools within 60 s")
            time.sleep(0.5)

    def alive(self) -> bool:
        try:
            _devtools(self.port, "/json/version")
            return True
        except OSError:
            return False

    def visit(self, site: Site) -> Signals:
        target = _devtools(self.port, "/json/new?about:blank", method="PUT")
        session = cdp.Session.connect(target["webSocketDebuggerUrl"])
        try:
            loaded, error = compat_page.load(session, site.url)
            if not loaded:
                return Signals(False, error=error, exceptions=compat_page.exceptions(session))
            values = compat_page.signals(session)
            failed, step_error = None, ""
            for i, step in enumerate(site.flow):
                try:
                    compat_page.run_step(session, step)
                except compat_page.StepFailed as e:
                    failed, step_error = i, str(e)
                    break
            return Signals(True, values["status"], compat_page.exceptions(session),
                           values["text"], values["height"], failed, step_error)
        finally:
            session.close()
            try:
                _devtools(self.port, f"/json/close/{target['id']}")
            except OSError:
                pass

    def stop(self) -> None:
        try:
            version = _devtools(self.port, "/json/version")
            cdp.Session.connect(version["webSocketDebuggerUrl"]).call("Browser.close")
        except (OSError, cdp.ProtocolError):
            pass
        try:
            self.process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            self.process.kill()
        shutil.rmtree(self.profile, ignore_errors=True)

    def restart_if_gone(self) -> None:
        if not self.alive():
            self.stop()
            self.start()


def _side(visit: Visit, site: Site) -> Signals:
    """A visit; a browser that fails mid-visit leaves a page that didn't load."""
    try:
        return visit(site)
    except (OSError, cdp.ProtocolError, ConnectionError, KeyError, TypeError) as e:
        return Signals(False, error=f"browser: {e}")


def _both(site: Site, shade: Visit, baseline: Visit) -> tuple[Signals, Signals]:
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        a = pool.submit(_side, shade, site)
        b = pool.submit(_side, baseline, site)
        return a.result(), b.result()


def run_site(site: Site, shade: Visit, baseline: Visit) -> dict:
    """One site's result: both browsers' signals, a rerun if broken, the verdict."""
    s, b = _both(site, shade, baseline)
    first = v.judge(s, b)
    rerun = None
    second = None
    if first[0] == v.BROKEN:
        s2, b2 = _both(site, shade, baseline)
        second = v.judge(s2, b2)
        rerun = {"shade": s2.to_dict(), "baseline": b2.to_dict()}
    verdict, why = v.settle(first, second, site.reason)
    return {"url": site.url, "category": site.category, "verdict": verdict, "why": why,
            "shade": s.to_dict(), "baseline": b.to_dict(), "rerun": rerun}


def run_level(sites: list[Site], level: str, shade_exe: Path, baseline_exe: Path,
              launcher: Path | None, extra: list[str]) -> list[dict]:
    shade = Browser(shade_exe, launcher, preferences(level), extra)
    baseline = Browser(baseline_exe, launcher, None, extra)
    shade.start()
    baseline.start()
    results = []
    try:
        for i, site in enumerate(sites, 1):
            result = run_site(site, shade.visit, baseline.visit)
            results.append(result)
            print(f"[{level} {i}/{len(sites)}] {result['verdict']:>8} {site.url}"
                  + (f": {'; '.join(result['why'])}" if result["why"] else ""), flush=True)
            shade.restart_if_gone()
            baseline.restart_if_gone()
    finally:
        shade.stop()
        baseline.stop()
    return results


def self_test_sites(base: str) -> list[Site]:
    flow = (Step("expect_visible", selector="#ready"),)
    return [Site(f"{base}/blocked.html", "news", flow), Site(f"{base}/fine.html", "news", flow)]


def self_test_problems(results: list[dict]) -> list[str]:
    expected = {"blocked.html": v.BROKEN, "fine.html": v.PASSED}
    problems = []
    for r in results:
        want = expected[r["url"].rsplit("/", 1)[1]]
        if r["verdict"] != want:
            problems.append(f"{r['url']}: {r['verdict']}, expected {want}")
    return problems


def select(sites: list[Site], urls: list[str], categories: list[str]) -> list[Site]:
    unknown = set(urls) - {s.url for s in sites}
    if unknown:
        raise ValueError(f"not in the corpus: {', '.join(sorted(unknown))}")
    return [s for s in sites if (not urls or s.url in urls)
            and (not categories or s.category in categories)]


def _serve(directory: Path) -> http.server.ThreadingHTTPServer:
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), functools.partial(Quiet, directory=str(directory)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("baseline")
    p.add_argument("--dir", type=Path, default=DEFAULT_DIR)
    p = sub.add_parser("run")
    p.add_argument("--shade", type=Path, required=True)
    p.add_argument("--baseline", type=Path, required=True)
    p.add_argument("--launcher", type=Path)
    p.add_argument("--level", action="append", choices=("standard", "strict", "off"))
    p.add_argument("--sites", nargs="*", default=[])
    p.add_argument("--category", nargs="*", default=[])
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--out", type=Path, default=Path("compat-report.json"))
    p = sub.add_parser("report")
    p.add_argument("report", type=Path)
    args = parser.parse_args(argv)

    if args.command == "baseline":
        version = (REPO / "CHROMIUM_VERSION").read_text(encoding="utf-8").strip()
        print(fetch_baseline(version, args.dir))
        return 0
    if args.command == "report":
        data = json.loads(args.report.read_text(encoding="utf-8"))
        sys.stdout.write(v.report(data["levels"]))
        return 0

    levels = args.level or ["standard", "strict"]
    server = None
    extra: list[str] = []
    if args.self_test:
        server = _serve(SITE_DIR)
        port = server.server_address[1]
        extra = ["--host-resolver-rules=" + ", ".join(f"MAP {h} 127.0.0.1"
                                                       for h in SELF_TEST_HOSTS)]
        sites = self_test_sites(f"http://{SELF_TEST_HOSTS[0]}:{port}")
        levels = ["standard"]
    else:
        sites = select(compat_corpus.load(), args.sites, args.category)
    try:
        data = {"shade": str(args.shade), "baseline": str(args.baseline),
                "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                "levels": {level: run_level(sites, level, args.shade, args.baseline,
                                            args.launcher, extra) for level in levels}}
    finally:
        if server:
            server.shutdown()
    args.out.write_bytes((json.dumps(data, indent=1) + "\n").encode("utf-8"))
    sys.stdout.write(v.report(data["levels"]))
    if args.self_test:
        problems = self_test_problems(data["levels"]["standard"])
        for problem in problems:
            print(f"SELF-TEST FAILED: {problem}")
        return 1 if problems else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
