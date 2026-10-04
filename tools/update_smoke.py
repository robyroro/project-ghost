#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Updater end to end in Windows Sandbox (Phase 2, sub-project B).

  sandbox (--offline-installer O | --online-installer U) --release-version V1
          [--update-crx C] [--update-version V2] --appid A
          [--server URL [--server-ssh USER@HOST]]
          [--tagged] [--codesign-cert CERT] [--cup-key K]
          [--recovery-crx R --recovery-version V3]
      on the build machine: run the test in a fresh Windows Sandbox. Without
      --server it runs tools/update_server.py in the sandbox; with it, the
      sandbox gets network access and uses that server, whose root
      certificate --server-ssh fetches, and which it then checks for the
      test machine's address.
  run     the test itself, in the sandbox

The test installs Ghost from the offline installer (V1), lets the updater
take the update from tools/update_server.py (V2, a CRX3), checks every
request against the allow-list and the updater's log for other hosts, then
uninstalls the browser and checks the updater removes itself.

With --codesign-cert, the sandbox trusts that certificate and every installed
PE file must be signed by it (sub-project D). With --tagged, the installer
runs with only --silent, so its tag must name the app. --recovery-crx is a
second update, signed with the backup publisher key, which the updater must
also take.
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

import authenticode
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
OFFLINE_INSTALLER = offline_installer.OUTPUT_NAME
CUP_KEY = "cup_key.json"
CODESIGN_CERT = "codesign.cer"
RECOVERY_CRX = "recovery.crx3"
ONLINE_INSTALLER = "UpdaterSetup.exe"
SERVER_ROOT_CERT = "server_root.crt"
LOCAL_SERVER = "http://127.0.0.1"
# Caddy's internal CA on the update server (sub-project C), until it has a domain.
CADDY_ROOT_CERT = "/var/lib/caddy/.local/share/caddy/pki/authorities/local/root.crt"


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


def foreign_urls(log: str, allowed: tuple[str, ...] = (LOCAL_SERVER,)) -> list[str]:
    return [url for url in _URL_RE.findall(log)
            if not url.startswith(allowed + _XML_NAMESPACE_PREFIXES)]


def client_address(ssh_connection: str) -> str:
    """This machine's address as a server sees it: SSH_CONNECTION's first field."""
    fields = ssh_connection.split()
    if len(fields) != 4:
        raise ValueError("unexpected SSH_CONNECTION")
    return fields[0]


def _ssh(host: str, command: str) -> subprocess.CompletedProcess:
    return subprocess.run(["ssh", host, command], capture_output=True, timeout=120)


def server_records(host: str) -> list[str]:
    """Where the server holds this machine's address, outside admin records."""
    address = client_address(_ssh(host, "echo $SSH_CONNECTION").stdout.decode())
    out = _ssh(host, f"sudo ghost-update-admin find-address {address}")
    if out.returncode == 0:
        return []
    return out.stdout.decode().splitlines() or [f"find-address exited with {out.returncode}"]


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


def signature_failures(dirs: list[Path], thumb: str) -> list[str]:
    files = [p for d in dirs for p in authenticode.pe_files(d)]
    return authenticode.verify(files, thumb, require_trusted=True) if files else [
        "no PE file to check"]


def run(payload: Path, results: Path, exp: smoke.Expectations, appid: str,
        update_version: str | None, server: str | None = None,
        installer: str = OFFLINE_INSTALLER, tagged: bool = False,
        recovery_version: str | None = None) -> dict:
    """Without `server`, serves update_version from payload/update.crx3 on loopback.
    Without `update_version` (the online installer), installs and checks only."""
    result = {"expectations": exp.__dict__, "steps": []}
    log = results / "requests.jsonl"
    final = (replace(exp, release_version=recovery_version or update_version)
             if update_version else exp)

    def step(name: str, failures: list[str], **details) -> bool:
        result["steps"].append({"name": name, "failures": failures, **details})
        return not failures

    local = None
    if server is None:
        local = update_server.UpdateServer(
            ("127.0.0.1", 8484),
            update_server.Offer(appid, update_version, payload / "update.crx3",
                                "mini_installer.exe", "--verbose-logging --do-not-launch-chrome"),
            update_server.load_key(payload / CUP_KEY), log)
        threading.Thread(target=local.serve_forever, daemon=True).start()
    try:
        if not step("clean machine", smoke.evaluate_uninstalled(smoke.snapshot(exp), exp)
                    + (["an updater is already installed"] if _updater_exe(exp) else [])):
            return result

        root_cert = payload / SERVER_ROOT_CERT
        if root_cert.exists():
            out = subprocess.run(["certutil", "-addstore", "-f", "Root", str(root_cert)],
                                 capture_output=True, text=True)
            if not step("trust the server", [] if out.returncode == 0 else [
                    f"certutil exited with {out.returncode}: {out.stdout.strip()[-300:]}"]):
                return result

        thumb = None
        codesign = payload / CODESIGN_CERT
        if codesign.exists():
            failures = []
            for store in ("Root", "TrustedPublisher"):
                out = subprocess.run(["certutil", "-addstore", "-f", store, str(codesign)],
                                     capture_output=True, text=True)
                failures += [] if out.returncode == 0 else [
                    f"certutil -addstore {store} exited with {out.returncode}"]
            if not step("trust the signing certificate", failures):
                return result
            thumb = authenticode.thumbprint(codesign.read_bytes())

        work = Path(tempfile.mkdtemp(prefix="installer-"))
        setup = Path(shutil.copy(payload / installer, work))
        arguments = ["--silent"] if tagged else offline_installer.install_arguments(appid)
        code = subprocess.run([str(setup), *arguments, "--enable-logging"],
                              timeout=1800).returncode
        installed = smoke.snapshot(exp)
        failures = [] if code == 0 else [f"{installer} exited with {code}"]
        failures += smoke.evaluate_installed(installed, exp)
        updater = _updater_exe(exp)
        failures += [] if updater else ["no updater.exe under the company directory"]
        failures += [] if _updater_tasks(exp) else ["no scheduled task for the updater"]
        pv = _registered_version(exp, appid)
        failures += [] if pv == exp.release_version else [
            f"Clients\\{appid} pv is {pv!r}, expected {exp.release_version!r}"]
        if not step("install", failures, snapshot=installed):
            return result
        app_dir = Path(installed["chrome_exe"]).parent
        if thumb and not step("signatures", signature_failures([app_dir, updater.parent],
                                                               thumb)):
            return result

        def update_to(version: str, name: str) -> bool:
            code = subprocess.run([str(updater), "--update-apps", "--enable-logging"],
                                  timeout=1800).returncode
            reached = _wait(lambda: _registered_version(exp, appid) == version, 1200)
            failures = [] if reached else [
                f"pv stayed {_registered_version(exp, appid)!r}, expected {version!r}"
                f" (updater exited with {code})"]
            failures += [] if (app_dir / version).is_dir() else [
                f"no {version} directory beside chrome.exe"]
            return step(name, failures)

        if update_version:
            if not update_to(update_version, "update"):
                return result
            if recovery_version:
                local.offer = update_server.Offer(
                    appid, recovery_version, payload / RECOVERY_CRX, "mini_installer.exe",
                    "--verbose-logging --do-not-launch-chrome")
                if not update_to(recovery_version, "recovery update (backup publisher key)"):
                    return result
            if thumb and not step("signatures after the update",
                                  signature_failures([app_dir], thumb)):
                return result
        step("launch", smoke.launch(Path(installed["chrome_exe"]), final))

        updater_log = "".join(p.read_text(encoding="utf-8", errors="replace")
                              for p in _company_dir(exp).rglob("updater*.log"))
        failures = [f"the updater's log names {url}" for url in foreign_urls(
            updater_log, (LOCAL_SERVER,) if server is None else (server,))]
        if server is None:
            lines = log.read_text(encoding="utf-8").splitlines() if log.exists() else []
            failures = evaluate_requests(lines) + failures
        step("privacy", failures)

        setup_exe = app_dir / final.release_version / "Installer" / "setup.exe"
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
        if local:
            local.shutdown()
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


def run_in_sandbox(installer: Path, crx: Path | None, release_version: str,
                   update_version: str | None, appid: str, timeout: int,
                   server: str | None = None, server_ssh: str | None = None,
                   cup_key: Path = update_server.CUP_KEY_FILE,
                   codesign_cert: Path | None = None, tagged: bool = False,
                   recovery_crx: Path | None = None,
                   recovery_version: str | None = None) -> int:
    payload = Path(tempfile.mkdtemp(prefix="update-payload-"))
    shutil.copy(installer, payload / installer.name)
    if crx:
        shutil.copy(crx, payload / "update.crx3")
    # Only tools/ is mapped into the sandbox; the server's key travels with the payload.
    shutil.copy(cup_key, payload / CUP_KEY)
    if codesign_cert:
        shutil.copy(codesign_cert, payload / CODESIGN_CERT)
    if recovery_crx:
        shutil.copy(recovery_crx, payload / RECOVERY_CRX)
    if server_ssh:
        cert = _ssh(server_ssh, f"sudo cat {CADDY_ROOT_CERT}")
        if cert.returncode or not cert.stdout:
            print("could not fetch the server's root certificate over SSH", file=sys.stderr)
            return 1
        (payload / SERVER_ROOT_CERT).write_bytes(cert.stdout)
    results = Path(tempfile.mkdtemp(prefix="update-smoke-"))
    exp = smoke.expectations(repo.REPO_ROOT, release_version)
    (results / EXPECTATIONS_FILE).write_text(json.dumps(exp.__dict__), encoding="utf-8")
    script = (f"update_smoke.py run --payload {smoke._IN_SANDBOX['installer']} "
              f"--results {smoke._IN_SANDBOX['results']} --appid {appid} "
              f"--installer {installer.name}"
              + (f" --update-version {update_version}" if update_version else "")
              + (f" --server {server}" if server else "")
              + (" --tagged" if tagged else "")
              + (f" --recovery-version {recovery_version}" if recovery_version else ""))
    config = results.with_suffix(".wsb")
    config.write_text(smoke.sandbox_config(payload, TOOLS_DIR, Path(sys.base_prefix), results,
                                           script_args=script, networking=server is not None),
                      encoding="utf-8")
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
    passed = smoke.passed(result)
    if server_ssh:
        found = server_records(server_ssh)
        print("ok      the server holds no record of this machine's address" if not found else
              "FAILED  the server holds this machine's address\n"
              + "\n".join(f"          - {place}" for place in found))
        passed = passed and not found
    return 0 if passed else 1


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="command", required=True)
    s = sub.add_parser("sandbox")
    s.add_argument("--offline-installer", type=Path)
    s.add_argument("--online-installer", type=Path)
    s.add_argument("--release-version", required=True,
                   help="the version the installer installs")
    s.add_argument("--update-crx", type=Path)
    s.add_argument("--update-version")
    s.add_argument("--appid", required=True)
    s.add_argument("--server", help="an update server's base URL, such as https://203.0.113.5")
    s.add_argument("--server-ssh", help="USER@HOST of that server")
    s.add_argument("--timeout", type=int, default=3600)
    s.add_argument("--tagged", action="store_true",
                   help="the installer carries its tag; run it with only --silent")
    s.add_argument("--codesign-cert", type=Path,
                   help="trust this certificate and require it on every installed PE file")
    s.add_argument("--cup-key", type=Path, default=update_server.CUP_KEY_FILE,
                   help="the CUP key the local server signs with")
    s.add_argument("--recovery-crx", type=Path,
                   help="a second update, signed with the backup publisher key")
    s.add_argument("--recovery-version")
    r = sub.add_parser("run")
    r.add_argument("--payload", type=Path, required=True)
    r.add_argument("--results", type=Path, required=True)
    r.add_argument("--appid", required=True)
    r.add_argument("--installer", default=OFFLINE_INSTALLER)
    r.add_argument("--update-version")
    r.add_argument("--server")
    r.add_argument("--tagged", action="store_true")
    r.add_argument("--recovery-version")
    return p


def argument_problem(args: argparse.Namespace) -> str | None:
    if (args.recovery_crx is None) != (args.recovery_version is None):
        return "pass --recovery-crx and --recovery-version together"
    if args.recovery_crx and args.server:
        return "the recovery drill serves its package itself; no --server"
    if (args.offline_installer is None) == (args.online_installer is None):
        return "pass one of --offline-installer and --online-installer"
    if args.online_installer:
        if not args.server:
            return "the online installer needs --server"
        if args.update_version:
            return "the online installer installs the server's release; no --update-version"
        return None
    if not args.update_version:
        return "the offline installer's test needs --update-version"
    if not args.server and not args.update_crx:
        return "without --server, the test serves --update-crx itself"
    return None


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    if args.command == "sandbox":
        problem = argument_problem(args)
        if problem:
            print(problem, file=sys.stderr)
            return 2
        installer = args.offline_installer or args.online_installer
        return run_in_sandbox(installer, None if args.server else args.update_crx,
                              args.release_version, args.update_version, args.appid,
                              args.timeout, args.server, args.server_ssh, args.cup_key,
                              args.codesign_cert, args.tagged, args.recovery_crx,
                              args.recovery_version)
    if not smoke.is_disposable(os.environ.get("USERNAME", ""), False):
        print("run installs into this user's profile; use `sandbox`", file=sys.stderr)
        return 2
    exp = smoke.Expectations(**json.loads(
        (args.results / EXPECTATIONS_FILE).read_text(encoding="utf-8")))
    result = run(args.payload, args.results, exp, args.appid, args.update_version,
                 args.server, args.installer, args.tagged, args.recovery_version)
    print(smoke.format_result(result))
    return 0 if smoke.passed(result) else 1


if __name__ == "__main__":
    sys.exit(main())
