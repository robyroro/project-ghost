#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Builds Ghost's offline installer: UpdaterSetup.exe with the browser inside.

Upstream's chrome/updater/win/signing/sign.py packs the browser installer
and an offline manifest into the metainstaller. It needs pywin32, which
Chromium's vpython3 environment (src/.vpython3) provides. With --sign, it
signs updater.exe and the metainstaller with a certificate from the My store
(sub-project D), and this tool then writes the tag, so the installer runs
with no arguments. Without --sign, the result is unsigned and untagged, and
the install arguments go on its command line (install_arguments()).

  python tools/offline_installer.py --src C:\\...\\src --out out\\vanilla
      --version 152.0.7977.14901 --appid {...} --output ProjectGhostOfflineSetup.exe
      [--sign "Project Ghost Test Code Signing"]
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from dataclasses import dataclass
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

OUTPUT_NAME = "ProjectGhostOfflineSetup.exe"
DEPOT_TOOLS = Path(r"C:\src\depot_tools")


@dataclass(frozen=True)
class Signing:
    signtool: Path
    subject: str  # the certificate's subject name in the My store
    service: str  # the RFC 3161 timestamp service


def tag_string(appid: str) -> str:
    return f"appguid={appid}&appname=Project%20Ghost&needsadmin=False"


def sign_command(depot_tools: Path, out_dir: Path, installer: Path, manifest: Path, appid: str,
                 version: str, output: Path, signing: Signing | None = None) -> list[str]:
    signing_dir = out_dir / "UpdaterSigning"
    replacements = {"${INSTALLER_VERSION}": version, "${ARCH_REQUIREMENT}": "x64"}
    command = [str(depot_tools / "vpython3.bat"), str(signing_dir / "sign.py"),
               "--in_file", str(out_dir / "UpdaterSetup.exe"), "--out_file", str(output),
               "--appid", appid, "--installer_path", str(installer),
               "--manifest_path", str(manifest), "--lzma_7z", str(signing_dir / "7zr.exe"),
               "--manifest_dict_replacements", repr(replacements)]
    if signing is None:
        return command + ["--disable_tag_and_sign"]
    flags = ["/fd", "SHA256", "/tr", signing.service, "/td", "SHA256"]
    return command + ["--signtool", str(signing.signtool), "--identity", signing.subject,
                      "--tagging_exe", str(out_dir / "tag.exe")] + [
                          f"--sign_flags={flag}" for flag in flags]


def tag_command(out_dir: Path, signed: Path, output: Path, appid: str) -> list[str]:
    return [str(out_dir / "tag.exe"), f"--set-tag={tag_string(appid)}", f"--out={output}",
            str(signed)]


def read_tag(out_dir: Path, exe: Path) -> str:
    out = subprocess.run([str(out_dir / "tag.exe"), "--get-tag", str(exe)],
                         capture_output=True, text=True)
    return out.stdout.strip() if out.returncode == 0 else ""


def install_arguments(appid: str) -> list[str]:
    return [f"--install={tag_string(appid)}", "--silent"]


def build(src: Path, out: Path, installer: Path, version: str, appid: str, output: Path,
          signing: Signing | None = None, depot_tools: Path = DEPOT_TOOLS) -> int:
    out_dir = (src / out).resolve()
    output.resolve().parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        manifest = Path(tmp) / "OfflineManifest.gup"
        manifest.write_text(MANIFEST, encoding="utf-8", newline="\n")
        target = Path(tmp) / "signed.exe" if signing else output.resolve()
        code = subprocess.run(sign_command(depot_tools, out_dir, installer.resolve(), manifest,
                                           appid, version, target, signing),
                              cwd=out_dir / "UpdaterSigning").returncode
        if code or signing is None:
            return code
        return subprocess.run(tag_command(out_dir, target, output.resolve(), appid)).returncode


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
    parser.add_argument("--sign", metavar="SUBJECT",
                        help="sign with this certificate from the My store, then tag")
    parser.add_argument("--signtool", type=Path)
    parser.add_argument("--depot-tools", type=Path, default=DEPOT_TOOLS)
    args = parser.parse_args(argv)
    out_dir = (args.src / args.out).resolve()
    signing = None
    if args.sign:
        import authenticode
        signing = Signing(args.signtool or authenticode.signtool_path(), args.sign,
                          authenticode.TIMESTAMP_SERVICES[0])
    return build(args.src, args.out, args.installer or out_dir / "mini_installer.exe",
                 args.version, args.appid, args.output, signing, args.depot_tools)


if __name__ == "__main__":
    sys.exit(main())
