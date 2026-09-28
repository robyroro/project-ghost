#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Reports whether this machine can build the pinned Chromium on Windows.

Probing (registry, vswhere, file versions) is kept separate from evaluation so
the rules can be tested with synthetic machines. Requirements come from
build/requirements.json, which is re-derived from upstream docs whenever
CHROMIUM_VERSION changes; nothing version-specific is hard-coded here.

Usage: python tools/check_env.py [--build-root D:\\ghost] [--json]
"""

from __future__ import annotations

import argparse
import dataclasses
import enum
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

import repo


class Status(str, enum.Enum):
    PASS = "PASS"
    WARN = "WARN"
    FAIL = "FAIL"
    INFO = "INFO"


@dataclass(frozen=True)
class CheckResult:
    name: str
    status: Status
    detail: str
    remedy: str = ""


@dataclass(frozen=True)
class VsInstall:
    version: str
    path: str
    product: str


@dataclass(frozen=True)
class PathEntry:
    path: str
    has_gclient: bool = False
    has_python: bool = False
    has_git: bool = False


@dataclass(frozen=True)
class VolumeFacts:
    path: str
    free_gb: float
    filesystem: str | None


@dataclass
class Facts:
    platform: str
    windows_major: int | None = None
    ram_gb: float | None = None
    logical_cpus: int | None = None
    vswhere_found: bool = False
    vs_installs: list[VsInstall] = field(default_factory=list)
    # Installs that satisfy both the version range and the required components.
    vs_satisfying: list[VsInstall] = field(default_factory=list)
    # The directory Chromium's vs_toolchain.py would pick, or None.
    vs_selected_path: str | None = None
    sdk_versions: list[str] = field(default_factory=list)
    sdk_include_versions: list[str] = field(default_factory=list)
    debugger_version: str | None = None
    git_version: str | None = None
    git_config: dict[str, str | None] = field(default_factory=dict)
    long_paths_enabled: bool | None = None
    path_entries: list[PathEntry] = field(default_factory=list)
    env: dict[str, str | None] = field(default_factory=dict)
    volume: VolumeFacts | None = None


# --- Evaluation (pure) -------------------------------------------------------

def evaluate(req: dict, facts: Facts) -> list[CheckResult]:
    if facts.platform != "win32":
        return [CheckResult(
            "platform", Status.FAIL, f"running on {facts.platform}",
            "The MVP build host is Windows 11 x64; run this on the build machine.")]
    win = req["windows"]
    results = [
        _check_windows(win, facts),
        _check_ram(win, facts),
        CheckResult("cpu", Status.INFO,
                    f"{facts.logical_cpus} logical CPUs (upstream: 20+ cores is not excessive)"),
        _check_visual_studio(win, facts),
        _check_sdk(win, facts),
        _check_debugger(win, facts),
        _check_git(win, facts),
    ]
    results += _check_git_config(win, facts)
    results += [
        _check_long_paths(facts),
        _check_depot_tools_on_path(facts),
    ]
    results += _check_env(win, facts)
    results.append(_check_volume(win, facts))
    return results


def _check_windows(win: dict, facts: Facts) -> CheckResult:
    minimum = win["min_windows_major"]
    if facts.windows_major is not None and facts.windows_major >= minimum:
        return CheckResult("windows", Status.PASS, f"Windows major version {facts.windows_major}")
    return CheckResult("windows", Status.FAIL, f"Windows major version {facts.windows_major}",
                       f"Windows {minimum} or newer is required.")


def _check_ram(win: dict, facts: Facts) -> CheckResult:
    ram = facts.ram_gb
    if ram is None:
        return CheckResult("ram", Status.WARN, "could not read installed memory")
    detail = f"{ram:.1f} GB"
    if ram < win["ram_gb"]["minimum"]:
        return CheckResult("ram", Status.FAIL, detail,
                           f"At least {win['ram_gb']['minimum']} GB is required.")
    if ram < win["ram_gb"]["recommended"]:
        return CheckResult("ram", Status.WARN, detail,
                           f"More than {win['ram_gb']['recommended']} GB is recommended.")
    return CheckResult("ram", Status.PASS, detail)


def chromium_vs_selection(vs: dict, env: dict[str, str | None],
                          exists: Callable[[str], bool]) -> str | None:
    """Returns the VS directory build/vs_toolchain.py would use, or None.

    Chromium does not ask vswhere; it probes fixed directories, so an install
    that vswhere reports can still be invisible to the build (for example
    Build Tools 2022, which installs under Program Files (x86) by default).
    """
    detection = vs["chromium_detection"]
    for entry in detection["years"]:
        candidates = []
        override = env.get(f"vs{entry['year']}_install")
        if override:
            candidates.append(override)
        base = _expand_windows_vars(entry["base"], env)
        candidates += [f"{base}\\{edition}" for edition in detection["editions"]]
        for path in candidates:
            if exists(path):
                return path
    return None


def _expand_windows_vars(text: str, env: dict[str, str | None]) -> str:
    return re.sub(r"%([^%]+)%", lambda m: env.get(m.group(1)) or m.group(0), text)


def _normalize_dir(path: str) -> str:
    return path.replace("/", "\\").rstrip("\\").lower()


def _check_visual_studio(win: dict, facts: Facts) -> CheckResult:
    vs = win["visual_studio"]
    components = vs["required_components"]
    install_remedy = ("Install Visual Studio 2026 (any edition) with the 'Desktop development "
                      "with C++' workload and 'MFC/ATL support': "
                      + " ".join(f"--add {c}" for c in components) + " --includeRecommended")
    if not facts.vs_satisfying:
        if not facts.vs_installs:
            return CheckResult("visual_studio", Status.FAIL, "no Visual Studio installation found",
                               install_remedy)
        listed = "; ".join(f"{i.product} {i.version} at {i.path}" for i in facts.vs_installs)
        return CheckResult(
            "visual_studio", Status.FAIL,
            f"found {listed}; none has {', '.join(components)} in {vs['version_range']}",
            "Add the missing components with Visual Studio Installer > Modify "
            f"(e.g. {components[-1]}), or: {install_remedy}")
    satisfying = {_normalize_dir(i.path): i for i in facts.vs_satisfying}
    selected = facts.vs_selected_path
    usable = next(iter(facts.vs_satisfying))
    year = next((e["year"] for e in vs["chromium_detection"]["years"]
                 if repo.parse_version(usable.version)[0] == e["major"]), "2022")
    if selected is None:
        return CheckResult(
            "visual_studio", Status.FAIL,
            f"{usable.product} {usable.version} at {usable.path} is not where "
            "build/vs_toolchain.py looks",
            f"Set the user environment variable vs{year}_install={usable.path}")
    if _normalize_dir(selected) not in satisfying:
        return CheckResult(
            "visual_studio", Status.FAIL,
            f"build/vs_toolchain.py would pick {selected}, which lacks the required components",
            f"Set vs{year}_install={usable.path} (vs_toolchain.py tries 2026 before 2022).")
    found = satisfying[_normalize_dir(selected)]
    return CheckResult("visual_studio", Status.PASS, f"{found.product} {found.version} at {selected}")


def _check_sdk(win: dict, facts: Facts) -> CheckResult:
    sdk = win["windows_sdk"]
    minimum = sdk["min_version"]
    # Chromium's build looks for Include/<include_version>; a newer SDK line
    # (a different build number) does not satisfy it, only a newer servicing
    # release of the same line does.
    same_line = [v for v in facts.sdk_versions
                 if repo.parse_version(v)[:3] == repo.parse_version(minimum)[:3]]
    good = [v for v in same_line if repo.version_at_least(v, minimum)]
    remedy = f"Install Windows SDK {minimum} (Visual Studio Installer or standalone SDK installer)."
    if sdk["include_version"] not in facts.sdk_include_versions:
        return CheckResult("windows_sdk", Status.FAIL,
                           f"Include\\{sdk['include_version']} not found "
                           f"(have: {', '.join(facts.sdk_include_versions) or 'none'})", remedy)
    if not good:
        return CheckResult("windows_sdk", Status.FAIL,
                           f"installed SDKs {', '.join(facts.sdk_versions) or 'none'}; need >= {minimum}",
                           remedy)
    return CheckResult("windows_sdk", Status.PASS, f"SDK {max(good, key=repo.parse_version)}")


def _check_debugger(win: dict, facts: Facts) -> CheckResult:
    minimum = win["debugging_tools"]["min_version"]
    remedy = ("Add 'Debugging Tools for Windows' via Settings > Apps > Windows Software "
              "Development Kit > Modify. Needed to read Chromium's >4 GiB PDBs.")
    if facts.debugger_version is None:
        return CheckResult("debugging_tools", Status.FAIL, "cdb.exe not found", remedy)
    if not repo.version_at_least(facts.debugger_version, minimum):
        return CheckResult("debugging_tools", Status.FAIL,
                           f"cdb.exe {facts.debugger_version} < {minimum}", remedy)
    return CheckResult("debugging_tools", Status.PASS, f"cdb.exe {facts.debugger_version}")


def _check_git(win: dict, facts: Facts) -> CheckResult:
    if facts.git_version is None:
        return CheckResult("git", Status.FAIL, "git not found", "Install Git for Windows.")
    recommended = win["git"]["recommended_version"]
    if not repo.version_at_least(facts.git_version, recommended):
        return CheckResult("git", Status.WARN, f"git {facts.git_version}",
                           f"depot_tools recommends git {recommended} or later; "
                           "update Git for Windows.")
    return CheckResult("git", Status.PASS, f"git {facts.git_version}")


def _check_git_config(win: dict, facts: Facts) -> list[CheckResult]:
    results = []
    for key, rule in win["git_config"].items():
        actual = facts.git_config.get(key)
        expected = rule["expected"]
        name = f"git_config:{key}"
        if actual is not None and actual.lower() == expected.lower():
            results.append(CheckResult(name, Status.PASS, f"{key}={actual}"))
            continue
        status = Status.FAIL if rule["severity"] == "fail" else Status.WARN
        results.append(CheckResult(name, status, f"{key}={actual if actual is not None else '<unset>'}",
                                   f"git config --global {key} {expected}"))
    return results


def _check_long_paths(facts: Facts) -> CheckResult:
    if facts.long_paths_enabled:
        return CheckResult("long_paths", Status.PASS, "LongPathsEnabled=1")
    detail = "unknown" if facts.long_paths_enabled is None else "LongPathsEnabled=0"
    return CheckResult("long_paths", Status.WARN, detail,
                       "Enable Win32 long paths (requires an administrator; you perform this).")


def _check_depot_tools_on_path(facts: Facts) -> CheckResult:
    entries = facts.path_entries
    depot = next((i for i, e in enumerate(entries) if e.has_gclient), None)
    if depot is None:
        return CheckResult(
            "depot_tools_path", Status.WARN, "depot_tools is not on PATH",
            "tools/bootstrap.py installs depot_tools under the build root; afterwards put it at "
            "the FRONT of the system PATH, ahead of any Python or Git.")
    shadowing = [e.path for e in entries[:depot] if e.has_python or e.has_git]
    if shadowing:
        return CheckResult(
            "depot_tools_path", Status.FAIL,
            f"{entries[depot].path} comes after {', '.join(shadowing)}",
            "Move depot_tools to the front of the SYSTEM PATH; a user-level PATH entry "
            "cannot precede a system-level Python or Git.")
    return CheckResult("depot_tools_path", Status.PASS, entries[depot].path)


def _check_env(win: dict, facts: Facts) -> list[CheckResult]:
    results = []
    for key, expected in win["env"].items():
        actual = facts.env.get(key)
        if actual == expected:
            results.append(CheckResult(f"env:{key}", Status.PASS, f"{key}={actual}"))
        else:
            results.append(CheckResult(
                f"env:{key}", Status.FAIL, f"{key}={actual if actual is not None else '<unset>'}",
                f"Set the user environment variable {key}={expected} so depot_tools uses the "
                "locally installed Visual Studio instead of Google's internal toolchain."))
    return results


def _check_volume(win: dict, facts: Facts) -> CheckResult:
    vol = facts.volume
    if vol is None:
        return CheckResult("build_root", Status.INFO, "no --build-root given; disk not checked")
    disk = win["disk_free_gb"]
    detail = f"{vol.path}: {vol.free_gb:.0f} GB free on {vol.filesystem or 'unknown filesystem'}"
    if " " in vol.path:
        return CheckResult("build_root", Status.FAIL, detail,
                           "Chromium requires a checkout path without spaces.")
    if vol.filesystem not in win["filesystems"]:
        return CheckResult("build_root", Status.FAIL, detail,
                           f"Use a volume formatted as {' or '.join(win['filesystems'])} "
                           "(a Dev Drive is ReFS).")
    if vol.free_gb < disk["minimum"]:
        return CheckResult("build_root", Status.FAIL, detail,
                           f"At least {disk['minimum']} GB free is required; "
                           f"{disk['recommended']} GB is recommended.")
    if vol.free_gb < disk["recommended"]:
        return CheckResult("build_root", Status.WARN, detail,
                           f"{disk['recommended']} GB free is recommended ({disk['note']})")
    return CheckResult("build_root", Status.PASS, detail)


# --- Probing -----------------------------------------------------------------

def _run(argv: list[str], cwd: str | None = None) -> str | None:
    try:
        proc = subprocess.run(argv, cwd=cwd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return proc.stdout if proc.returncode == 0 else None


def _program_files_x86() -> Path:
    return Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))


def probe_vs(req_vs: dict) -> tuple[bool, list[VsInstall], list[VsInstall]]:
    vswhere = _program_files_x86() / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return False, [], []

    def query(extra: list[str]) -> list[VsInstall]:
        out = _run([str(vswhere), "-products", "*", "-format", "json", "-utf8", *extra])
        if not out:
            return []
        return [VsInstall(i.get("installationVersion", "?"), i.get("installationPath", "?"),
                          i.get("productId", "?")) for i in json.loads(out)]

    everything = query(["-all", "-prerelease"])
    satisfying = query(["-version", req_vs["version_range"],
                        "-requires", *req_vs["required_components"]])
    return True, everything, satisfying


def probe_sdk_versions() -> list[str]:
    import winreg
    pattern = re.compile(r"^Windows Software Development Kit - Windows (\d+\.\d+\.\d+\.\d+)$")
    found = set()
    for subkey in (r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",
                   r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"):
        try:
            root = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, subkey)
        except OSError:
            continue
        with root:
            for i in range(winreg.QueryInfoKey(root)[0]):
                try:
                    with winreg.OpenKey(root, winreg.EnumKey(root, i)) as k:
                        name = winreg.QueryValueEx(k, "DisplayName")[0]
                except OSError:
                    continue
                m = pattern.match(str(name))
                if m:
                    found.add(m.group(1))
    return sorted(found, key=repo.parse_version)


def probe_sdk_include_versions() -> list[str]:
    include = _program_files_x86() / "Windows Kits" / "10" / "Include"
    if not include.is_dir():
        return []
    return sorted(d.name for d in include.iterdir() if (d / "um" / "windows.h").is_file())


def probe_file_version(path: Path) -> str | None:
    """Reads the fixed file version from a PE resource via version.dll."""
    if not path.is_file():
        return None
    import ctypes
    from ctypes import wintypes
    version = ctypes.WinDLL("version")
    size = version.GetFileVersionInfoSizeW(str(path), None)
    if not size:
        return None
    buf = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(str(path), 0, size, buf):
        return None

    class VS_FIXEDFILEINFO(ctypes.Structure):
        _fields_ = [(n, wintypes.DWORD) for n in (
            "dwSignature", "dwStrucVersion", "dwFileVersionMS", "dwFileVersionLS",
            "dwProductVersionMS", "dwProductVersionLS", "dwFileFlagsMask", "dwFileFlags",
            "dwFileOS", "dwFileType", "dwFileSubtype", "dwFileDateMS", "dwFileDateLS")]

    ptr = ctypes.c_void_p()
    length = wintypes.UINT()
    if not version.VerQueryValueW(buf, "\\", ctypes.byref(ptr), ctypes.byref(length)):
        return None
    info = ctypes.cast(ptr, ctypes.POINTER(VS_FIXEDFILEINFO)).contents
    ms, ls = info.dwFileVersionMS, info.dwFileVersionLS
    return f"{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}"


def probe_ram_gb() -> float | None:
    import ctypes
    from ctypes import wintypes

    class MEMORYSTATUSEX(ctypes.Structure):
        _fields_ = [("dwLength", wintypes.DWORD), ("dwMemoryLoad", wintypes.DWORD),
                    ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                    ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                    ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                    ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]

    status = MEMORYSTATUSEX()
    status.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        return None
    return status.ullTotalPhys / 2**30


def probe_long_paths() -> bool | None:
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            r"SYSTEM\CurrentControlSet\Control\FileSystem") as k:
            return winreg.QueryValueEx(k, "LongPathsEnabled")[0] == 1
    except OSError:
        return None


def probe_git(keys: list[str]) -> tuple[str | None, dict[str, str | None]]:
    out = _run(["git", "--version"])
    m = re.search(r"git version (\d+\.\d+\.\d+)", out or "")
    if not m:
        return None, {}
    # Run outside any repository so only system and global config apply.
    cwd = tempfile.gettempdir()
    config = {}
    for key in keys:
        value = _run(["git", "config", "--get", key], cwd=cwd)
        config[key] = value.strip() if value is not None else None
    return m.group(1), config


def probe_path_entries() -> list[PathEntry]:
    entries = []
    for raw in os.environ.get("PATH", "").split(os.pathsep):
        if not raw:
            continue
        p = Path(os.path.expandvars(raw))
        entries.append(PathEntry(
            raw,
            has_gclient=(p / "gclient.bat").is_file(),
            has_python=(p / "python.exe").is_file() or (p / "python3.exe").is_file(),
            has_git=(p / "git.exe").is_file()))
    return entries


def probe_volume(path: str) -> VolumeFacts:
    """Free space and filesystem of the volume that holds (or would hold) path."""
    existing = Path(path)
    while not existing.exists() and existing.parent != existing:
        existing = existing.parent
    free_gb = shutil.disk_usage(existing).free / 2**30
    return VolumeFacts(path, free_gb, _filesystem_of(existing))


def _filesystem_of(path: Path) -> str | None:
    if sys.platform != "win32":
        return None
    import ctypes
    k32 = ctypes.windll.kernel32
    mount = ctypes.create_unicode_buffer(1024)
    if not k32.GetVolumePathNameW(str(path.resolve()), mount, len(mount)):
        return None
    fs_name = ctypes.create_unicode_buffer(64)
    ok = k32.GetVolumeInformationW(mount.value, None, 0, None, None, None, fs_name, len(fs_name))
    return fs_name.value if ok else None


def collect_facts(req: dict, build_root: str | None) -> Facts:
    facts = Facts(platform=sys.platform, logical_cpus=os.cpu_count())
    if sys.platform != "win32":
        return facts
    win = req["windows"]
    facts.windows_major = sys.getwindowsversion().major
    facts.ram_gb = probe_ram_gb()
    facts.vswhere_found, facts.vs_installs, facts.vs_satisfying = probe_vs(win["visual_studio"])
    vs_env = {k: os.environ.get(k) for k in ("ProgramFiles", "vs2026_install", "vs2022_install")}
    facts.vs_selected_path = chromium_vs_selection(win["visual_studio"], vs_env, os.path.isdir)
    facts.sdk_versions = probe_sdk_versions()
    facts.sdk_include_versions = probe_sdk_include_versions()
    facts.debugger_version = probe_file_version(
        _program_files_x86() / "Windows Kits" / "10" / "Debuggers" / "x64" / "cdb.exe")
    facts.git_version, facts.git_config = probe_git(list(win["git_config"]))
    facts.long_paths_enabled = probe_long_paths()
    facts.path_entries = probe_path_entries()
    facts.env = {key: os.environ.get(key) for key in win["env"]}
    if build_root:
        facts.volume = probe_volume(build_root)
    return facts


# --- CLI ---------------------------------------------------------------------

def format_report(results: list[CheckResult]) -> str:
    width = max(len(r.name) for r in results)
    lines = [f"{r.status.value:4}  {r.name:<{width}}  {r.detail}" for r in results]
    todo = [r for r in results if r.remedy and r.status in (Status.FAIL, Status.WARN)]
    if todo:
        lines += ["", "To fix:"]
        lines += [f"  [{r.status.value}] {r.name}: {r.remedy}" for r in todo]
    failures = sum(r.status is Status.FAIL for r in results)
    warnings = sum(r.status is Status.WARN for r in results)
    lines += ["", f"{failures} failure(s), {warnings} warning(s)."]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-root", help="directory that will hold the Chromium checkout")
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    args = parser.parse_args(argv)

    req = repo.load_requirements()
    results = evaluate(req, collect_facts(req, args.build_root))
    if args.json:
        print(json.dumps([dict(dataclasses.asdict(r), status=r.status.value) for r in results],
                         indent=2))
    else:
        print(f"Requirements for Chromium {req['chromium_version']}\n")
        print(format_report(results))
    return 1 if any(r.status is Status.FAIL for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
