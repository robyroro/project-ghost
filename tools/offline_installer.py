#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Builds Ghost's offline installer: UpdaterSetup.exe with the browser inside.

Upstream's chrome/updater/win/signing/sign.py packs the browser installer
and an offline manifest into the metainstaller. It needs pywin32, which
Chromium's vpython3 environment (src/.vpython3) provides. Until sub-project
D signs and tags installers, the result is unsigned and untagged, so the
install arguments go on its command line (install_arguments()).

  python tools/offline_installer.py --src C:\\...\\src --out out\\vanilla
      --version 152.0.7977.14901 --appid {...} --output ProjectGhostOfflineSetup.exe
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

# protocol 3.0 XML, as upstream's offline example; sign.py fills in ${...}.
MANIFEST = """<?xml version="1.0" encoding="UTF-8"?>
<response protocol="3.0">
  <systemrequirements platform="win" arch="${ARCH_REQUIREMENT}" min_os_version="10.0"/>
  <app appid="${APP_ID}" status="ok">
    <updatecheck status="ok">
      <urls>
        <url codebase="http://127.0.0.1:8484/download/"/>
      </urls>
      <manifest version="${INSTALLER_VERSION}">
        <packages>
          <package name="${INSTALLER_FILENAME}" hash_sha256="${INSTALLER_HASH_SHA256}" size="${INSTALLER_SIZE}" required="true"/>
        </packages>
        <actions>
          <action event="install" run="${INSTALLER_FILENAME}" arguments="--verbose-logging --do-not-launch-chrome" needsadmin="false"/>
          <action event="postinstall" onsuccess="exitsilentlyonlaunchcmd"/>
        </actions>
      </manifest>
    </updatecheck>
  </app>
</response>
"""


def sign_command(depot_tools: Path, out_dir: Path, installer: Path, manifest: Path, appid: str,
                 version: str, output: Path) -> list[str]:
    signing = out_dir / "UpdaterSigning"
    replacements = {"${INSTALLER_VERSION}": version, "${ARCH_REQUIREMENT}": "x64"}
    return [str(depot_tools / "vpython3.bat"), str(signing / "sign.py"),
            "--in_file", str(out_dir / "UpdaterSetup.exe"), "--out_file", str(output),
            "--appid", appid, "--installer_path", str(installer),
            "--manifest_path", str(manifest), "--lzma_7z", str(signing / "7zr.exe"),
            "--disable_tag_and_sign", "--manifest_dict_replacements", repr(replacements)]


def install_arguments(appid: str) -> list[str]:
    return [f"--install=appguid={appid}&appname=Project%20Ghost&needsadmin=False", "--silent"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--src", type=Path, required=True, help="Chromium checkout (src)")
    parser.add_argument("--out", type=Path, required=True,
                        help="output dir with UpdaterSetup.exe, relative to --src; a static "
                             "build, since the metainstaller runs alone")
    parser.add_argument("--version", required=True, help="the browser installer's version")
    parser.add_argument("--appid", required=True, help="the browser's app ID")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--installer", type=Path,
                        help="the browser installer (default: mini_installer.exe in --out)")
    parser.add_argument("--depot-tools", type=Path, default=Path(r"C:\src\depot_tools"))
    args = parser.parse_args(argv)
    out_dir = (args.src / args.out).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        manifest = Path(tmp) / "OfflineManifest.gup"
        manifest.write_text(MANIFEST, encoding="utf-8", newline="\n")
        installer = (args.installer or out_dir / "mini_installer.exe").resolve()
        command = sign_command(args.depot_tools, out_dir, installer,
                               manifest, args.appid, args.version, args.output.resolve())
        return subprocess.run(command, cwd=out_dir / "UpdaterSigning").returncode


if __name__ == "__main__":
    sys.exit(main())
