#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Authenticode signing of Ghost's PE files (sub-project D).

signtool signs with the certificate whose SHA-1 thumbprint it is given, from
the user's My store; its key may live in the TPM. Each signature gets an
RFC 3161 timestamp, so it stays valid after the certificate expires; the
second service is tried when the first fails. A timestamp request sends only
a hash.

verify() checks that each file is signed by a given certificate and that
Windows accepts the signature. Off the test machines the test certificate's
root isn't trusted, which require_trusted=False accepts.
"""

from __future__ import annotations

import ctypes
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
from typing import Iterable

import repo
from signing import SigningError

SIGNABLE = frozenset({".exe", ".dll"})
TIMESTAMP_SERVICES = ("http://timestamp.digicert.com", "http://timestamp.sectigo.com")
BATCH = 50
KITS = Path(r"C:\Program Files (x86)\Windows Kits\10\bin")
CERT_E_UNTRUSTEDROOT = 0x800B0109


def thumbprint(cert_der: bytes) -> str:
    return hashlib.sha1(cert_der).hexdigest().upper()


def signtool_path(kits: Path = KITS) -> Path:
    found = sorted(kits.glob("10.*/x64/signtool.exe"),
                   key=lambda p: repo.parse_version(p.parent.parent.name))
    if not found:
        raise SigningError(f"no signtool.exe under {kits}; install the Windows SDK")
    return found[-1]


def is_pe(path: Path) -> bool:
    if path.suffix.lower() not in SIGNABLE:
        return False
    with path.open("rb") as f:
        return f.read(2) == b"MZ"


def pe_files(root: Path) -> list[Path]:
    return sorted(p for p in Path(root).rglob("*") if p.is_file() and is_pe(p))


def sign_command(signtool: Path, thumb: str, service: str, files: Iterable[Path]) -> list[str]:
    return [str(signtool), "sign", "/q", "/fd", "SHA256", "/sha1", thumb, "/tr", service,
            "/td", "SHA256", *map(str, files)]


def sign(files: Iterable[Path], thumb: str, signtool: Path, run=subprocess.run,
         services: tuple[str, ...] = TIMESTAMP_SERVICES) -> None:
    files = list(files)
    for i in range(0, len(files), BATCH):
        batch, errors = files[i:i + BATCH], []
        for service in services:
            out = run(sign_command(signtool, thumb, service, batch), capture_output=True,
                      text=True)
            if out.returncode == 0:
                break
            errors.append(f"{service}: {(out.stdout + out.stderr).strip()[-300:]}")
        else:
            raise SigningError("signtool failed with every timestamp service:\n"
                               + "\n".join(errors))


# --- Verification ------------------------------------------------------------------

def _powershell(script: str) -> str:
    out = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
                         capture_output=True, text=True)
    if out.returncode:
        raise SigningError(f"PowerShell failed: {out.stderr.strip()[-300:]}")
    return out.stdout


def signer_thumbprints(files: list[Path]) -> dict[str, str | None]:
    with tempfile.TemporaryDirectory() as tmp:
        listing = Path(tmp) / "files.txt"
        listing.write_text("\n".join(str(f) for f in files), encoding="utf-8")
        text = _powershell(
            f"ConvertTo-Json -Compress -InputObject @(Get-Content -Encoding UTF8 -LiteralPath "
            # Get-Content's strings carry PSPath and friends; [string] drops them.
            f"'{listing}' | ForEach-Object {{ $p = [string]$_; "
            "$s = Get-AuthenticodeSignature -LiteralPath $p; "
            "@{Path = $p; Thumbprint = if ($s.SignerCertificate) "
            "{ $s.SignerCertificate.Thumbprint } else { $null }} })")
    return {entry["Path"]: entry["Thumbprint"] for entry in json.loads(text or "[]")}


def has_certificate(thumb: str) -> bool:
    """Whether the user's My store holds the certificate with its private key."""
    return _powershell(
        f"$c = Get-Item -LiteralPath 'Cert:\\CurrentUser\\My\\{thumb}' -ErrorAction "
        "SilentlyContinue; [bool]($c -and $c.HasPrivateKey)").strip() == "True"


class _Guid(ctypes.Structure):
    _fields_ = [("Data1", ctypes.c_uint32), ("Data2", ctypes.c_uint16),
                ("Data3", ctypes.c_uint16), ("Data4", ctypes.c_ubyte * 8)]


class _FileInfo(ctypes.Structure):
    _fields_ = [("cbStruct", ctypes.c_uint32), ("pcwszFilePath", ctypes.c_wchar_p),
                ("hFile", ctypes.c_void_p), ("pgKnownSubject", ctypes.c_void_p)]


class _TrustData(ctypes.Structure):
    _fields_ = [("cbStruct", ctypes.c_uint32), ("pPolicyCallbackData", ctypes.c_void_p),
                ("pSIPClientData", ctypes.c_void_p), ("dwUIChoice", ctypes.c_uint32),
                ("fdwRevocationChecks", ctypes.c_uint32), ("dwUnionChoice", ctypes.c_uint32),
                ("pFile", ctypes.POINTER(_FileInfo)), ("dwStateAction", ctypes.c_uint32),
                ("hWVTStateData", ctypes.c_void_p), ("pwszURLReference", ctypes.c_wchar_p),
                ("dwProvFlags", ctypes.c_uint32), ("dwUIContext", ctypes.c_uint32),
                ("pSignatureSettings", ctypes.c_void_p)]


# WINTRUST_ACTION_GENERIC_VERIFY_V2
_VERIFY_V2 = _Guid(0x00AAC56B, 0xCD44, 0x11D0,
                   (ctypes.c_ubyte * 8)(0x8C, 0xC2, 0x00, 0xC0, 0x4F, 0xC2, 0x95, 0xEE))
_WTD_UI_NONE, _WTD_CHOICE_FILE = 2, 1
_WTD_STATEACTION_VERIFY, _WTD_STATEACTION_CLOSE = 1, 2
_WTD_CACHE_ONLY_URL_RETRIEVAL = 0x1000


def trust_status(path: Path) -> int:
    """WinVerifyTrust's result for a file: 0, or an HRESULT such as CERT_E_UNTRUSTEDROOT."""
    wintrust = ctypes.WinDLL("wintrust")
    wintrust.WinVerifyTrust.argtypes = [ctypes.c_void_p, ctypes.POINTER(_Guid), ctypes.c_void_p]
    wintrust.WinVerifyTrust.restype = ctypes.c_long
    info = _FileInfo(ctypes.sizeof(_FileInfo), str(path), None, None)
    data = _TrustData(cbStruct=ctypes.sizeof(_TrustData), dwUIChoice=_WTD_UI_NONE,
                      dwUnionChoice=_WTD_CHOICE_FILE, pFile=ctypes.pointer(info),
                      dwStateAction=_WTD_STATEACTION_VERIFY,
                      dwProvFlags=_WTD_CACHE_ONLY_URL_RETRIEVAL)
    status = wintrust.WinVerifyTrust(None, ctypes.byref(_VERIFY_V2), ctypes.byref(data))
    data.dwStateAction = _WTD_STATEACTION_CLOSE
    wintrust.WinVerifyTrust(None, ctypes.byref(_VERIFY_V2), ctypes.byref(data))
    return status & 0xFFFFFFFF


def verify(files: Iterable[Path], thumb: str, require_trusted: bool) -> list[str]:
    files = [Path(f) for f in files]
    signers = signer_thumbprints(files)
    failures = []
    for f in files:
        signer = signers.get(str(f))
        if signer != thumb.upper():
            failures.append(f"{f.name}: signed by {signer or 'nobody'}")
            continue
        status = trust_status(f)
        if status == 0 or (status == CERT_E_UNTRUSTEDROOT and not require_trusted):
            continue
        failures.append(f"{f.name}: WinVerifyTrust 0x{status:08x}")
    return failures
