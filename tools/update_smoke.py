#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Updater end to end in Windows Sandbox (Phase 2, sub-project B).

  sandbox --offline-installer O --release-version V1 --update-crx C
          --update-version V2 --appid A
      on the build machine: run the test in a fresh Windows Sandbox
  run     the test itself, in the sandbox

The test installs Ghost from the offline installer (V1), lets the updater
take the update from tools/update_server.py (V2, a CRX3), checks every
request against the allow-list and the updater's log for other hosts, then
uninstalls the browser and checks the updater removes itself.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import replace
from pathlib import Path

import installer_smoke as smoke
import offline_installer
import repo
import update_server

TOOLS_DIR = Path(__file__).resolve().parent
RESULT_FILE = smoke.RESULT_FILE
EXPECTATIONS_FILE = smoke.EXPECTATIONS_FILE
# URIs that name XML namespaces rather than hosts the updater contacts; its
# log holds its scheduled task's XML definition.
_XML_NAMESPACE_PREFIXES = ("http://schemas.microsoft.com/",)
_URL_RE = re.compile(r"https?://[^\s\"'<>]+")


def evaluate_requests(lines: list[str]) -> list[str]:
    failures = []
    for i, line in enumerate(lines, 1):
        body = json.loads(line)["body"] or {}
        failures += [f"request {i} carries {key}" for key in update_server.disallowed_keys(body)]
        # Every request must be an update check. The scrubber drops an event
        # request's `event` lists, so what shows one is an app without
        # `updatecheck`.
        apps = (body.get("request") or {}).get("apps") or []
        failures += [f"request {i} is not an update check: app {app.get('appid')} has no"
                     " updatecheck (an event request)"
                     for app in apps if "updatecheck" not in app]
    return failures or ([] if lines else ["the updater sent no request"])


def foreign_urls(log: str) -> list[str]:
    return [url for url in _URL_RE.findall(log)
            if not url.startswith(("http://127.0.0.1",) + _XML_NAMESPACE_PREFIXES)]


def _company_dir(exp: smoke.Expectations) -> Path:
    return Path(os.environ["LOCALAPPDATA"]) / exp.company_path


def _updater_exe(exp: smoke.Expectations) -> Path | None:
    found = sorted(_company_dir(exp).glob("ProjectGhostUpdater/*/updater.exe"))
    return found[-1] if found else None


def _registered_version(exp: smoke.Expectations, appid: str) -> str | None:
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                            rf"Software\{exp.company_path}\Update\Clients\{appid}") as key:
            return winreg.QueryValueEx(key, "pv")[0]
    except OSError:
        return None


def _updater_key_exists(exp: smoke.Expectations) -> bool:
    import winreg
    try:
        winreg.OpenKey(winreg.HKEY_CURRENT_USER, rf"Software\{exp.company_path}\Update").Close()
        return True
    except OSError:
        return False


def _updater_key_tree(exp: smoke.Expectations) -> dict[str, list[str]]:
    """Every subkey of Software\\<company>\\Update, relative, with its value names."""
    import winreg
    tree: dict[str, list[str]] = {}

    def walk(relative: str) -> None:
        path = rf"Software\{exp.company_path}\Update" + (f"\\{relative}" if relative else "")
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
                subkeys, values, _ = winreg.QueryInfoKey(key)
                tree[relative] = sorted(winreg.EnumValue(key, i)[0] for i in range(values))
                names = [winreg.EnumKey(key, i) for i in range(subkeys)]
        except OSError:
            return
        for name in names:
            walk(f"{relative}\\{name}" if relative else name)

    walk("")
    return tree


def _updater_tasks(exp: smoke.Expectations) -> list[str]:
    out = subprocess.run(["schtasks", "/query", "/v", "/fo", "csv"], capture_output=True,
                         text=True).stdout
    return [line for line in out.splitlines() if "ProjectGhostUpdater" in line]


def _wait(condition, seconds: int) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(5)
    return condition()


def run(payload: Path, results: Path, exp: smoke.Expectations, appid: str,
        update_version: str) -> dict:
    result = {"expectations": exp.__dict__, "steps": []}
    log = results / "requests.jsonl"
    updated = replace(exp, release_version=update_version)

    def step(name: str, failures: list[str], **details) -> bool:
        result["steps"].append({"name": name, "failures": failures, **details})
        return not failures

    server = update_server.UpdateServer(
        ("127.0.0.1", 8484),
        update_server.Offer(appid, update_version, payload / "update.crx3", "mini_installer.exe",
                            "--verbose-logging --do-not-launch-chrome"),
        update_server.load_key(payload / "cup_test_key.json"), log)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        if not step("clean machine", smoke.evaluate_uninstalled(smoke.snapshot(exp), exp)
                    + (["an updater is already installed"] if _updater_exe(exp) else [])):
            return result

        work = Path(tempfile.mkdtemp(prefix="offline-"))
        setup = Path(shutil.copy(payload / "ProjectGhostOfflineSetup.exe", work))
        code = subprocess.run([str(setup), *offline_installer.install_arguments(appid),
                               "--enable-logging"], timeout=900).returncode
        installed = smoke.snapshot(exp)
        failures = [] if code == 0 else [f"the offline installer exited with {code}"]
        failures += smoke.evaluate_installed(installed, exp)
        updater = _updater_exe(exp)
        failures += [] if updater else ["no updater.exe under the company directory"]
        failures += [] if _updater_tasks(exp) else ["no scheduled task for the updater"]
        pv = _registered_version(exp, appid)
        failures += [] if pv == exp.release_version else [
            f"Clients\\{appid} pv is {pv!r}, expected {exp.release_version!r}"]
        if not step("install", failures, snapshot=installed):
            return result

        code = subprocess.run([str(updater), "--update-apps", "--enable-logging"],
                              timeout=900).returncode
        reached = _wait(lambda: _registered_version(exp, appid) == update_version, 600)
        failures = [] if reached else [
            f"pv stayed {_registered_version(exp, appid)!r}, expected {update_version!r}"
            f" (updater exited with {code})"]
        app_dir = Path(installed["chrome_exe"]).parent
        failures += [] if (app_dir / update_version).is_dir() else [
            f"no {update_version} directory beside chrome.exe"]
        if not step("update", failures):
            return result
        step("launch", smoke.launch(Path(installed["chrome_exe"]), updated))

        lines = log.read_text(encoding="utf-8").splitlines() if log.exists() else []
        updater_log = "".join(p.read_text(encoding="utf-8", errors="replace")
                              for p in _company_dir(exp).rglob("updater*.log"))
        step("privacy", evaluate_requests(lines)
             + [f"the updater's log names {url}" for url in foreign_urls(updater_log)])

        setup_exe = app_dir / update_version / "Installer" / "setup.exe"
        code = subprocess.run([str(setup_exe), "--uninstall", "--force-uninstall",
                               "--verbose-logging"], timeout=900).returncode
        failures = ([] if code == smoke.UNINSTALL_SUCCESSFUL else
                    [f"setup.exe --uninstall exited with {code}"])
        # --wake notices the app is gone, then starts --uninstall-if-unused
        # itself; run alone, --uninstall-if-unused still counts the app.
        subprocess.run([str(updater), "--wake", "--enable-logging"], timeout=900)
        gone = _wait(lambda: _updater_exe(exp) is None, 300)
        uninstalled = smoke.snapshot(exp)
        failures += smoke.evaluate_uninstalled(uninstalled, exp)
        failures += [] if gone else ["the updater did not remove itself"]
        failures += ["the updater's scheduled task is left"] if _updater_tasks(exp) else []
        failures += ([f"Software\\{exp.company_path}\\Update is left"]
                     if _updater_key_exists(exp) else [])
        step("uninstall", failures, snapshot=uninstalled, updater_key=_updater_key_tree(exp))
    except Exception as e:  # reported, so the build machine learns why
        step("error", [f"{type(e).__name__}: {e}"])
    finally:
        server.shutdown()
        for name in ("chrome_installer.log",):
            path = Path(tempfile.gettempdir()) / name
            if path.exists():
                shutil.copy(path, results / name)
        for path in _company_dir(exp).rglob("updater*.log") if exp.company_path else []:
            shutil.copy(path, results / path.name)
        partial = results / (RESULT_FILE + ".partial")
        partial.write_text(json.dumps(result, indent=1), encoding="utf-8")
        partial.replace(results / RESULT_FILE)
    return result


def run_in_sandbox(offline: Path, crx: Path, release_version: str, update_version: str,
                   appid: str, timeout: int) -> int:
    payload = Path(tempfile.mkdtemp(prefix="update-payload-"))
    shutil.copy(offline, payload / "ProjectGhostOfflineSetup.exe")
    shutil.copy(crx, payload / "update.crx3")
    # Only tools/ is mapped into the sandbox; the server's key travels with the payload.
    shutil.copy(update_server.CUP_KEY_FILE, payload / "cup_test_key.json")
    results = Path(tempfile.mkdtemp(prefix="update-smoke-"))
    exp = smoke.expectations(repo.REPO_ROOT, release_version)
    (results / EXPECTATIONS_FILE).write_text(json.dumps(exp.__dict__), encoding="utf-8")
    script = (f"update_smoke.py run --payload {smoke._IN_SANDBOX['installer']} "
              f"--results {smoke._IN_SANDBOX['results']} --appid {appid} "
              f"--update-version {update_version}")
    config = results.with_suffix(".wsb")
    config.write_text(smoke.sandbox_config(payload, TOOLS_DIR, Path(sys.base_prefix), results,
                                           script_args=script), encoding="utf-8")
    print(f"starting Windows Sandbox; results in {results}", flush=True)
    subprocess.Popen([str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
                          / "WindowsSandbox.exe"), str(config)])
    deadline = time.monotonic() + timeout
    while not (results / RESULT_FILE).exists():
        if time.monotonic() > deadline:
            print(f"no result within {timeout}s", file=sys.stderr)
            return 1
        time.sleep(5)
    result = smoke.read_result(results)
    print(smoke.format_result(result))
    return 0 if smoke.passed(result) else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    s = sub.add_parser("sandbox")
    s.add_argument("--offline-installer", type=Path, required=True)
    s.add_argument("--release-version", required=True)
    s.add_argument("--update-crx", type=Path, required=True)
    s.add_argument("--update-version", required=True)
    s.add_argument("--appid", required=True)
    s.add_argument("--timeout", type=int, default=2700)
    r = sub.add_parser("run")
    r.add_argument("--payload", type=Path, required=True)
    r.add_argument("--results", type=Path, required=True)
    r.add_argument("--appid", required=True)
    r.add_argument("--update-version", required=True)
    args = parser.parse_args(argv)
    if args.command == "sandbox":
        return run_in_sandbox(args.offline_installer, args.update_crx, args.release_version,
                              args.update_version, args.appid, args.timeout)
    if not smoke.is_disposable(os.environ.get("USERNAME", ""), False):
        print("run installs into this user's profile; use `sandbox`", file=sys.stderr)
        return 2
    exp = smoke.Expectations(**json.loads(
        (args.results / EXPECTATIONS_FILE).read_text(encoding="utf-8")))
    result = run(args.payload, args.results, exp, args.appid, args.update_version)
    print(smoke.format_result(result))
    return 0 if smoke.passed(result) else 1


if __name__ == "__main__":
    sys.exit(main())
