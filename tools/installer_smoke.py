#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Installer smoke test: install for the current user, check, launch, uninstall, check.

  sandbox  on the build machine: run the test in a fresh Windows Sandbox, with
           no network, and report its result
  run      the test itself, in a disposable Windows machine

`run` installs into the current user's profile and registry, so it refuses
to run outside Windows Sandbox unless --disposable says the machine may be
thrown away.

What is checked comes from branding/, CHROMIUM_VERSION and the installer's
file version, which is the release version (ADR 0007):
- after install: the browser, under the company directory, and setup.exe in
  the release version's directory; the Apps & features entry with the product name, publisher and
  release version; registration as a browser (StartMenuInternet and the HTML
  ProgID); Start menu and Desktop shortcuts named for the product and opening
  it; the product name in chrome.exe's file properties; and nothing named
  Chromium, so Ghost can be installed next to Chromium;
- launch: the installed browser starts and reports CHROMIUM_VERSION, the
  version websites see;
- after uninstall: none of the above is left.
"""

from __future__ import annotations

import argparse
import json
import ntpath
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import xml.sax.saxutils
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import cdp
import repo

TOOLS_DIR = Path(__file__).resolve().parent
RESULT_FILE = "result.json"
EXPECTATIONS_FILE = "expectations.json"
SANDBOX_USER = "WDAGUtilityAccount"
# installer::InstallStatus (chrome/installer/util/util_constants.h).
FIRST_INSTALL_SUCCESS = 0
UNINSTALL_SUCCESSFUL = 19

# Where the build machine's folders appear inside the sandbox.
_IN_SANDBOX = {"installer": r"C:\ghost\installer", "tools": r"C:\ghost\tools",
               "python": r"C:\ghost\python", "results": r"C:\ghost\results"}


@dataclass(frozen=True)
class Expectations:
    product_path: str     # kProductPathName: install and registry directory name
    company_path: str     # kCompanyPathName: the directory above product_path, may be empty
    app_name: str         # base_app_name: StartMenuInternet key prefix
    prog_id_prefix: str   # browser_prog_id_prefix
    pdf_prog_id_prefix: str
    url_scheme: str       # direct_launch_url_scheme
    product_name: str     # BRANDING PRODUCT_FULLNAME: shortcuts, Apps & features
    company_name: str     # BRANDING COMPANY_FULLNAME: publisher
    release_version: str  # the installer's file version: install directory, Apps & features
    web_version: str      # CHROMIUM_VERSION: what the running browser reports

    @property
    def install_dir_parts(self) -> tuple[str, ...]:
        return tuple(p for p in (self.company_path, self.product_path) if p)

    @property
    def registry_root(self) -> str:
        return self.company_path or self.product_path

    @property
    def uninstall_key(self) -> str:
        return " ".join(self.install_dir_parts)


def expectations(root: Path, release_version: str) -> Expectations:
    modes = (root / "branding" / "install_modes.h").read_text(encoding="utf-8")

    def field(pattern: str) -> str:
        m = re.search(pattern, modes)
        if not m:
            raise ValueError(f"branding/install_modes.h: no match for {pattern}")
        return m.group(1)

    branding = {}
    for line in (root / "branding" / "BRANDING").read_text(encoding="utf-8").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            branding[key.strip()] = value.strip()
    return Expectations(
        product_path=field(r'kProductPathName\[\]\s*=\s*L"([^"]*)"'),
        company_path=field(r'kCompanyPathName\[\]\s*=\s*L"([^"]*)"'),
        app_name=field(r'\.base_app_name\s*=\s*L"([^"]*)"'),
        prog_id_prefix=field(r'\.browser_prog_id_prefix\s*=\s*L"([^"]*)"'),
        pdf_prog_id_prefix=field(r'\.pdf_prog_id_prefix\s*=\s*L"([^"]*)"'),
        url_scheme=field(r'\.direct_launch_url_scheme\s*=\s*"([^"]*)"'),
        product_name=branding["PRODUCT_FULLNAME"],
        company_name=branding["COMPANY_FULLNAME"],
        release_version=release_version,
        web_version=repo.read_chromium_version(root))


# --- Checks ---------------------------------------------------------------------
#
# A snapshot is the machine state that matters, as plain data (see snapshot()):
# chrome_exe, start_menu, desktop: paths
# files: {"chrome.exe": bool, "setup.exe": bool}
# uninstall: the Apps & features entry's values, or None
# software, start_menu_internet, classes: subkey names under HKCU\Software,
#   ...\Clients\StartMenuInternet and ...\Classes
# product_parent_keys: subkey names under HKCU\Software\<company>, or under
#   HKCU\Software without a company
# shortcuts: {path of each .lnk in the Start menu and on the Desktop: target}
# version_info: chrome.exe's ProductName and CompanyName, or None


def _same_path(a: str, b: str) -> bool:
    return ntpath.normcase(ntpath.normpath(a)) == ntpath.normcase(ntpath.normpath(b))


# What the installer registers under HKCU\Software: (what, the snapshot list it
# appears in, which subkey names it). Each must exist after install and be gone
# after uninstall.
def _registration_kinds(exp: Expectations) -> list[tuple[str, str, Callable[[str], bool]]]:
    return [
        ("StartMenuInternet", "start_menu_internet", lambda k: k.startswith(exp.app_name)),
        (f"{exp.prog_id_prefix}.* ProgID", "classes",
         lambda k: k.startswith(exp.prog_id_prefix + ".")),
        (f"{exp.pdf_prog_id_prefix}.* ProgID", "classes",
         lambda k: k.startswith(exp.pdf_prog_id_prefix + ".")),
        (f"{exp.url_scheme}: URL scheme", "classes", lambda k: k == exp.url_scheme),
        ("Software\\" + "\\".join(exp.install_dir_parts), "product_parent_keys",
         lambda k: k == exp.product_path),
    ]


# Software\<company> may outlive the browser: the updater keeps its Update key
# there, and upstream leaves the company key itself, as Chrome leaves
# Software\Google.
_COMPANY_KEYS_ALLOWED_AFTER_UNINSTALL = ("Update",)

# A normal uninstall keeps the profile, and upstream clears the product key
# only with it (RemoveDistributionRegistryState runs when the profile is
# deleted). What it leaves is the installer's taskbar pin state.
_PRODUCT_VALUES_KEPT_WITH_THE_PROFILE = ("InstallerPinned",)


def _kept_with_the_profile(snap: dict) -> bool:
    key = snap.get("product_key")
    return bool(key) and not key["subkeys"] and set(key["values"]) <= set(
        _PRODUCT_VALUES_KEPT_WITH_THE_PROFILE)


def _outside_company(snap: dict, exp: Expectations) -> list[str]:
    """A product key at the top of Software, beside the company's: a generic
    name such as Software\\Browser that another program could own."""
    if exp.company_path and exp.product_path in snap["software"]:
        return [f"Software\\{exp.product_path} is written outside Software\\{exp.company_path}"]
    return []


def _registrations(snap: dict, exp: Expectations) -> list[str]:
    return [f"{where}\\{k}" for _, where, matches in _registration_kinds(exp)
            for k in snap[where] if matches(k)]


def evaluate_installed(snap: dict, exp: Expectations) -> list[str]:
    failures = []
    if not snap["files"]["chrome.exe"]:
        failures.append(f"chrome.exe is not at {snap['chrome_exe']}")
    if not snap["files"]["setup.exe"]:
        failures.append(f"setup.exe is not in {exp.release_version}\\Installer")

    entry = snap["uninstall"]
    if entry is None:
        failures.append("no Apps & features entry")
    else:
        for name, want in (("DisplayName", exp.product_name), ("Publisher", exp.company_name),
                           ("DisplayVersion", exp.release_version)):
            if entry.get(name) != want:
                failures.append(f"Apps & features {name} is {entry.get(name)!r}, "
                                f"expected {want!r}")

    failures += [f"no {name}" for name, where, matches in _registration_kinds(exp)
                 if not any(matches(k) for k in snap[where])]

    for where, directory in (("Start menu", snap["start_menu"]), ("Desktop", snap["desktop"])):
        path = ntpath.join(directory, exp.product_name + ".lnk")
        target = next((t for p, t in snap["shortcuts"].items() if _same_path(p, path)), None)
        if target is None:
            failures.append(f"no {where} shortcut named {exp.product_name}.lnk")
        elif not _same_path(target, snap["chrome_exe"]):
            failures.append(f"the {where} shortcut opens {target}, not {snap['chrome_exe']}")

    info = snap["version_info"] or {}
    for name, want in (("ProductName", exp.product_name), ("CompanyName", exp.company_name)):
        if info.get(name) != want:
            failures.append(f"chrome.exe {name} is {info.get(name)!r}, expected {want!r}")

    names = (snap["software"] + snap["start_menu_internet"] + snap["classes"]
             + [ntpath.basename(p) for p in snap["shortcuts"]])
    failures += [f"{n} is named Chromium" for n in names if "chromium" in n.lower()]
    failures += _outside_company(snap, exp)
    return failures


def evaluate_uninstalled(snap: dict, exp: Expectations) -> list[str]:
    failures = [f"{name} is still installed" for name, present in snap["files"].items()
                if present]
    if snap["uninstall"] is not None:
        failures.append("the Apps & features entry is left behind")
    leftovers = _registrations(snap, exp)
    product_key = f"product_parent_keys\\{exp.product_path}"
    if exp.company_path and product_key in leftovers and _kept_with_the_profile(snap):
        leftovers.remove(product_key)
    failures += [f"{r} is left behind" for r in leftovers]
    failures += [f"shortcut {p} is left behind" for p, t in snap["shortcuts"].items()
                 if ntpath.basename(p).lower() == exp.product_name.lower() + ".lnk"
                 or _same_path(t, snap["chrome_exe"])]
    if exp.company_path:
        failures += [f"Software\\{exp.company_path}\\{k} is left behind"
                     for k in snap["product_parent_keys"]
                     if k != exp.product_path and k not in _COMPANY_KEYS_ALLOWED_AFTER_UNINSTALL]
    failures += _outside_company(snap, exp)
    return failures


def evaluate_launch(version: dict, exp: Expectations) -> list[str]:
    """`version` is DevTools' Browser.getVersion reply."""
    want = f"Chrome/{exp.web_version}"
    if version.get("product") != want:
        return [f"the browser reports {version.get('product')!r}, expected {want!r}"]
    return []


def passed(result: dict) -> bool:
    return bool(result["steps"]) and all(not s["failures"] for s in result["steps"])


def format_result(result: dict) -> str:
    lines = []
    for step in result["steps"]:
        lines.append(f"{'FAILED' if step['failures'] else 'ok':8}{step['name']}")
        lines += [f"          - {f}" for f in step["failures"]]
    lines.append("PASSED" if passed(result) else "FAILED")
    return "\n".join(lines)


def read_result(results_dir: Path) -> dict:
    return json.loads((results_dir / RESULT_FILE).read_text(encoding="utf-8"))


def is_disposable(user: str, flag: bool) -> bool:
    return flag or user == SANDBOX_USER


# --- Reading the machine (Windows only) -------------------------------------------

def _powershell_json(script: str):
    out = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                          script + " | ConvertTo-Json -Compress"],
                         capture_output=True, text=True, check=True).stdout.strip()
    return json.loads(out) if out else None


def _ps_quote(path: str) -> str:
    return "'" + path.replace("'", "''") + "'"


def installer_version(installer: Path) -> str:
    """The installer's file version: the chrome/VERSION of the build that made it."""
    return _powershell_json(f"(Get-Item -LiteralPath {_ps_quote(str(installer))})"
                            ".VersionInfo.FileVersion")


def snapshot(exp: Expectations) -> dict:
    import winreg

    def subkeys(path: str) -> list[str]:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
                return [winreg.EnumKey(key, i) for i in range(winreg.QueryInfoKey(key)[0])]
        except OSError:
            return []

    def values(path: str) -> dict | None:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
                return {winreg.EnumValue(key, i)[0]: winreg.EnumValue(key, i)[1]
                        for i in range(winreg.QueryInfoKey(key)[1])}
        except OSError:
            return None

    app_dir = Path(os.environ["LOCALAPPDATA"]).joinpath(*exp.install_dir_parts) / "Application"
    product_key_path = "Software\\" + "\\".join(exp.install_dir_parts)
    product_values = values(product_key_path)
    product_key = (None if product_values is None else
                   {"values": sorted(product_values), "subkeys": subkeys(product_key_path)})
    start_menu = Path(os.environ["APPDATA"]) / "Microsoft/Windows/Start Menu/Programs"
    desktop = Path(os.environ["USERPROFILE"]) / "Desktop"
    links = sorted(str(p) for p in list(start_menu.rglob("*.lnk")) + list(desktop.glob("*.lnk")))
    targets = []
    if links:
        targets = _powershell_json(
            "$shell = New-Object -ComObject WScript.Shell; @("
            + ",".join(f"$shell.CreateShortcut({_ps_quote(p)}).TargetPath" for p in links) + ")")
        targets = targets if isinstance(targets, list) else [targets]
    chrome = app_dir / "chrome.exe"
    info = None
    if chrome.exists():
        info = _powershell_json(f"(Get-Item -LiteralPath {_ps_quote(str(chrome))}).VersionInfo"
                                " | Select-Object ProductName, CompanyName")
    return {
        "chrome_exe": str(chrome), "start_menu": str(start_menu), "desktop": str(desktop),
        "files": {"chrome.exe": chrome.exists(),
                  "setup.exe": (app_dir / exp.release_version / "Installer"
                                / "setup.exe").exists()},
        "uninstall": values(rf"Software\Microsoft\Windows\CurrentVersion\Uninstall"
                            rf"\{exp.uninstall_key}"),
        "software": subkeys("Software"),
        # The keys beside the product's own: under Software\<company>, or at
        # the top of Software without a company.
        "product_parent_keys": subkeys("Software\\" + exp.company_path
                                       if exp.company_path else "Software"),
        "product_key": product_key,
        "start_menu_internet": subkeys(r"Software\Clients\StartMenuInternet"),
        "classes": subkeys(r"Software\Classes"),
        "shortcuts": dict(zip(links, targets)),
        "version_info": info,
    }


# --- Running the test (in the disposable machine) ------------------------------------

def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def launch(chrome: Path, exp: Expectations) -> list[str]:
    """Starts the installed browser, asks it its version over DevTools, closes it."""
    with tempfile.TemporaryDirectory(prefix="smoke-profile-", ignore_cleanup_errors=True) as prof:
        port = _free_port()
        proc = subprocess.Popen([str(chrome), f"--user-data-dir={prof}",
                                 f"--remote-debugging-port={port}", "--no-first-run",
                                 "about:blank"])
        try:
            deadline = time.monotonic() + 60
            while True:
                try:
                    with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/version",
                                                timeout=5) as response:
                        endpoint = json.load(response)["webSocketDebuggerUrl"]
                    break
                except OSError:
                    if time.monotonic() > deadline or proc.poll() is not None:
                        return ["the installed browser did not start within 60s"]
                    time.sleep(1)
            session = cdp.Session.connect(endpoint)
            version = session.call("Browser.getVersion")
            try:
                session.call("Browser.close")
            except (ConnectionError, cdp.ProtocolError):
                pass  # the browser may close the connection before replying
            proc.wait(timeout=30)
            return evaluate_launch(version, exp)
        finally:
            if proc.poll() is None:
                subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                               capture_output=True)


def stage_installer(installer: Path, work_dir: Path) -> Path:
    """Copies the installer somewhere writable, as a download would be.

    mini_installer unpacks into a directory it creates beside itself, so it
    cannot run from the read-only folder the sandbox maps.
    """
    return Path(shutil.copy(installer, work_dir / installer.name))


def run(installer: Path, results_dir: Path, exp: Expectations) -> dict:
    result = {"expectations": exp.__dict__, "steps": []}

    def step(name: str, failures: list[str], **details) -> bool:
        result["steps"].append({"name": name, "failures": failures, **details})
        return not failures

    try:
        if not step("clean machine", evaluate_uninstalled(snapshot(exp), exp)):
            return result
        installer = stage_installer(installer, Path(tempfile.mkdtemp(prefix="installer-")))
        code = subprocess.run([str(installer), "--do-not-launch-chrome", "--verbose-logging"],
                              timeout=900).returncode
        installed = snapshot(exp)
        failures = ([] if code == FIRST_INSTALL_SUCCESS else
                    [f"mini_installer exited with {code}, expected {FIRST_INSTALL_SUCCESS}"])
        if not step("install", failures + evaluate_installed(installed, exp),
                    snapshot=installed):
            return result
        step("launch", launch(Path(installed["chrome_exe"]), exp))
        setup = (Path(installed["chrome_exe"]).parent / exp.release_version / "Installer"
                 / "setup.exe")
        code = subprocess.run([str(setup), "--uninstall", "--force-uninstall",
                               "--verbose-logging"], timeout=900).returncode
        uninstalled = snapshot(exp)
        failures = ([] if code == UNINSTALL_SUCCESSFUL else
                    [f"setup.exe --uninstall exited with {code}, expected {UNINSTALL_SUCCESSFUL}"])
        step("uninstall", failures + evaluate_uninstalled(uninstalled, exp),
             snapshot=uninstalled)
    except Exception as e:  # reported, so the build machine learns why
        step("error", [f"{type(e).__name__}: {e}"])
    finally:
        log = Path(tempfile.gettempdir()) / "chrome_installer.log"
        if log.exists():
            shutil.copy(log, results_dir / log.name)
        # Written whole, then renamed: the build machine waits for this file.
        partial = results_dir / (RESULT_FILE + ".partial")
        partial.write_text(json.dumps(result, indent=1), encoding="utf-8")
        partial.replace(results_dir / RESULT_FILE)
    return result


# --- Driving Windows Sandbox (on the build machine) ------------------------------------

def sandbox_config(installer_dir: Path, tools_dir: Path, python_dir: Path, results_dir: Path,
                   installer_name: str = "mini_installer.exe",
                   script_args: str | None = None) -> str:
    """A Windows Sandbox configuration that runs `script_args` (a script in
    tools/ and its arguments), or this smoke test, then shuts down."""
    def folder(host: Path, key: str, read_only: bool) -> str:
        return ("    <MappedFolder>\n"
                f"      <HostFolder>{xml.sax.saxutils.escape(str(host))}</HostFolder>\n"
                f"      <SandboxFolder>{_IN_SANDBOX[key]}</SandboxFolder>\n"
                f"      <ReadOnly>{'true' if read_only else 'false'}</ReadOnly>\n"
                "    </MappedFolder>\n")

    # The run's output goes to run.log: a run that fails before writing its
    # result would otherwise leave no trace once the sandbox shuts down.
    if script_args is None:
        script_args = (f'installer_smoke.py run '
                       f'--installer {_IN_SANDBOX["installer"]}\\{installer_name} '
                       f'--results {_IN_SANDBOX["results"]}')
    command = (f'cmd.exe /c "{_IN_SANDBOX["python"]}\\python.exe '
               f'{_IN_SANDBOX["tools"]}\\{script_args} '
               f'> {_IN_SANDBOX["results"]}\\run.log 2>&1 & shutdown /s /t 0"')
    return ("<Configuration>\n"
            "  <Networking>Disable</Networking>\n"
            "  <vGPU>Disable</vGPU>\n"
            "  <MappedFolders>\n"
            + folder(installer_dir, "installer", True) + folder(tools_dir, "tools", True)
            + folder(python_dir, "python", True) + folder(results_dir, "results", False)
            + "  </MappedFolders>\n"
            "  <LogonCommand>\n"
            f"    <Command>{xml.sax.saxutils.escape(command)}</Command>\n"
            "  </LogonCommand>\n"
            "</Configuration>\n")


def run_in_sandbox(installer: Path, timeout: int) -> int:
    sandbox = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "WindowsSandbox.exe"
    if not sandbox.exists():
        print("Windows Sandbox is not enabled. Enable it as an administrator, then restart:\n"
              "  Enable-WindowsOptionalFeature -Online -FeatureName "
              "Containers-DisposableClientVM", file=sys.stderr)
        return 2
    results = Path(tempfile.mkdtemp(prefix="installer-smoke-"))
    (results / EXPECTATIONS_FILE).write_text(
        json.dumps(expectations(repo.REPO_ROOT, installer_version(installer)).__dict__),
        encoding="utf-8")
    config = results.with_suffix(".wsb")
    config.write_text(sandbox_config(installer.resolve().parent, TOOLS_DIR,
                                     Path(sys.base_prefix), results, installer.name),
                      encoding="utf-8")
    print(f"starting Windows Sandbox; results in {results}", flush=True)
    subprocess.Popen([str(sandbox), str(config)])
    deadline = time.monotonic() + timeout
    while not (results / RESULT_FILE).exists():
        if time.monotonic() > deadline:
            print(f"no result within {timeout}s", file=sys.stderr)
            return 1
        time.sleep(5)
    result = read_result(results)
    print(format_result(result))
    return 0 if passed(result) else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    s = sub.add_parser("sandbox")
    s.add_argument("--installer", type=Path, required=True, help="path to mini_installer.exe")
    s.add_argument("--timeout", type=int, default=1800, help="seconds to wait for a result")
    r = sub.add_parser("run")
    r.add_argument("--installer", type=Path, required=True)
    r.add_argument("--results", type=Path, required=True, help="directory for result.json")
    r.add_argument("--disposable", action="store_true",
                   help="this machine may be thrown away (implied in Windows Sandbox)")
    args = parser.parse_args(argv)

    if args.command == "sandbox":
        return run_in_sandbox(args.installer, args.timeout)
    if not is_disposable(os.environ.get("USERNAME", ""), args.disposable):
        print("run installs into this user's profile; use `sandbox`, or pass --disposable "
              "on a machine that may be thrown away", file=sys.stderr)
        return 2
    saved = args.results / EXPECTATIONS_FILE
    exp = (Expectations(**json.loads(saved.read_text(encoding="utf-8"))) if saved.exists()
           else expectations(repo.REPO_ROOT, installer_version(args.installer)))
    result = run(args.installer, args.results, exp)
    print(format_result(result))
    return 0 if passed(result) else 1


if __name__ == "__main__":
    sys.exit(main())
